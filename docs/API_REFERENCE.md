# predictfun-cpp API reference

This document maps the installed C++20 API by authority. It is intentionally
organized around what a linked target is allowed to do, not around transport
implementation details. Public headers live under `include/predictfun/` and
installed CMake targets use the `predictfun::` namespace.

## Package and executor model

```cmake
find_package(predictfun 0.1.0 EXACT CONFIG REQUIRED)
target_link_libraries(my_reader PRIVATE
    predictfun::public_rest
    predictfun::public_wss
    predictfun::analysis)
```

Every asynchronous client accepts a caller-owned Boost.Asio executor. Handlers
receive `predictfun::Result<T>` and are invoked on that executor. The caller
owns the event loop, client lifetime and credential providers. `RequestContext`
adds a steady-clock deadline, optional `std::stop_token` and non-secret trace
id to each HTTP operation.

## Authority layers

| Target | Principal headers | Authority |
|---|---|---|
| `predictfun::types` | `types/*.hpp` | Exact values and domain types only |
| `predictfun::codec` | `codec/*.hpp` | Bounded encode/decode only |
| `predictfun::net` | `net/http.hpp`, `net/websocket.hpp` | Generic HTTPS/WSS transport |
| `predictfun::public_rest` | `public_rest/client.hpp` | Public read-only REST |
| `predictfun::public_wss` | `public_wss/client.hpp` | Public read-only streaming |
| `predictfun::analysis` | `analysis/liquidity.hpp` | Offline, read-only depth analysis |
| `predictfun::auth` | `auth/client.hpp` | Challenge/proof JWT acquisition |
| `predictfun::private_rest` | `private_rest/client.hpp` | Authenticated account reads; one explicit referral mutation |
| `predictfun::private_wss` | `private_wss/client.hpp` | Authenticated wallet/order events |
| `predictfun::order` | `order/*.hpp` | Deterministic unsigned order construction and hashing |
| `predictfun::local_signer` | `order/local_signer.hpp` | Optional in-memory ECDSA signing |
| `predictfun::trading` | `trading/client.hpp` | Explicit create/remove mutations |
| `predictfun::lifecycle` | `lifecycle/*.hpp` | Order state, journal and reconciliation |
| `predictfun::execution` | `execution/session.hpp` | Durable single-attempt order submission composition |
| `predictfun::chain` | `chain/*.hpp` | BNB JSON-RPC reads and transaction construction/execution |
| `predictfun::local_transaction_signer` | `chain/local_transaction_signer.hpp` | Optional in-memory EIP-155 transaction signing |

Link the narrowest target that satisfies the host. A read-only application
does not need and should not link any signer, trading or chain-write target.
Building with `PREDICTFUN_BUILD_LOCAL_SIGNER=OFF` also omits signer headers and
exported signer targets from the installed package.

## Exact numeric model

The venue numeric API never requires binary floating point:

- `FixedDecimal` retains integer units plus a decimal scale up to 18;
- `Price` stores integer ticks at a market's `decimalPrecision`;
- `Uint256` stores a canonical unsigned base-10 integer;
- `ExactDecimal` retains a canonical decimal string for values that may exceed
  native integer ranges.

Use `parse` and inspect the returned `Result`; do not reconstruct protocol
amounts through `double`. The canonical orderbook is the YES book.
`derive_no_book` derives NO bids/asks by exact tick complementation without
mutating the source snapshot.

## Result and error handling

`Result<T>` is a value-or-`Error` result. An `Error` exposes a typed
`ErrorCode`, optional HTTP status, sanitized message/field metadata and never
contains credentials. A transport timeout on a mutation is not a rejection:
mutation APIs return a `MutationOutcome<T>` whose disposition distinguishes
acknowledged, rejected and ambiguous outcomes.

Safe host rule:

1. retry bounded idempotent reads according to client policy;
2. never infer mutation failure from timeout/disconnect;
3. never blindly resend an ambiguous create order;
4. reconcile the deterministic hash through authenticated state.

## Public REST

`public_rest::PublicRestClient` provides:

- markets and one market;
- categories and one category;
- canonical YES orderbook;
- paginated and latest timeseries;
- matches, tags, statistics and last sale;
- text search;
- public positions for an EVM address.

`ClientOptions` selects BNB testnet/mainnet, accepts an API-key provider, decode
limits, shared rate limiter and bounded GET retry count. Share one
`net::RateLimiter` between clients that use the same key so endpoint and global
budgets remain process-wide. Mainnet requires a caller-provided key. Request
targets reject secrets and redirects are not followed.

## Public WebSocket

`public_wss::PublicWsClient` manages read-only subscriptions:

```text
start(topics, event_ready) -> try_pop_event() -> stop()
subscribe(topic) / unsubscribe(topic)
```

The client bounds frames and queued events, echoes the exact heartbeat,
resubscribes after reconnect, tracks freshness and exposes generation/reconnect
statistics. `event_ready` is a wake-up notification; drain `try_pop_event()` on
the caller executor. A generation change invalidates assumptions based on an
old stream snapshot.

## Offline liquidity analysis

`analysis/liquidity.hpp` exposes exact same-snapshot estimates:

- `quote_market_buy_value`: spend collateral across sorted asks;
- `quote_market_sell_shares`: sell shares across sorted bids;
- `quote_immediate_round_trip`: isolate visible spread/depth loss.

Insufficient but valid depth returns `complete=false`, not an error. Invalid
sorting, price or quantity fails with a typed error. Fees are deliberately not
included; apply the venue fee model separately.

## Authentication and authenticated reads

`auth::AuthClient` gets the challenge, exchanges an `AuthProof`, or performs
both through a caller-supplied asynchronous `MessageSigner`. JWTs use
move-only `SecretString` ownership.

`private_rest::PrivateRestClient` reads account, positions, orders, one order
and activity. `async_set_referral` is the sole mutation in this target and is
strictly single-attempt with explicit ambiguous-result semantics.

`private_wss::PrivateWsClient` streams wallet/order events. It has no initial
authoritative account snapshot. After every generation change, load complete
REST state and call `mark_reconciled(generation)` only after queued events and
that snapshot have been applied.

## Orders, signing and trading

`order::OrderBuilder` builds integer-only `UnsignedOrder` values and their
EIP-712 digest. `BuilderOptions` supplies chain id, salt, clock and optional
Predict Account. The optional `LocalSigner` accepts a private key only through
an explicit move-only `SecretString`; it never reads files or environment
variables.

`trading::TradingClient` exposes only explicit mutations:

- `async_create_order`;
- `async_remove_order_ids`;
- `async_remove_order_hashes`.

The client validates complete signed payloads before dispatch. Create/remove
operations are single-attempt; the result must be interpreted through its
mutation disposition.

## Durable lifecycle and execution

`lifecycle::OrderJournal` is append-only, length-bounded and checksummed. A
truncated final record is ignored/repaired during recovery; corruption inside
a completed record fails closed. It never persists keys, JWTs, signatures,
raw request bodies or raw venue responses.

`lifecycle::PersistentOrderTracker` journals a transition before publishing it
in memory. Restarted nonterminal orders are quarantined pending a complete REST
snapshot.

Prefer `execution::DurableOrderSession` for live hosts. It composes authenticated
clients with the journal and guarantees:

- deterministic hash journaled before the one create attempt;
- known rejection and ambiguous outcome persisted;
- serialized wallet/book events;
- bounded full-order reconciliation after each private WSS generation change.

See [DURABLE_ORDER_SESSION.md](DURABLE_ORDER_SESSION.md) for the operational
sequence and failure semantics.

## BNB chain

`chain::ChainClient` supplies typed chain id, native/ERC-20/ERC-1155 reads,
allowance/approval checks, gas/nonce population, receipt polling and explicit
transaction execution. `chain/operations.hpp` constructs standard and
negative-risk split, merge, redeem, convert and cancellation transactions for
EOA or Predict Account routes.

The optional `LocalTransactionSigner` is a separate target. Raw submission is
never silently retried after an ambiguous response. Caller-authorized testnet
acceptance is documented in [TESTNET_ACCEPTANCE.md](TESTNET_ACCEPTANCE.md).

## Installed tools versus library API

Tools are operator diagnostics, not hidden SDK authority:

- `predictfun_read_only_probe`: public REST smoke probe;
- `predictfun_public_wss_probe`: public stream freshness probe;
- `predictfun_btc_liquidity_probe/report`: credential-free liquidity evidence;
- `predictfun_recovery_example`: offline journal inspection;
- `predictfun_testnet_acceptance`: explicitly gated chain-97 acceptance harness
  when local signing is enabled.

The compile-checked cookbook examples are described in
[COOKBOOK.md](COOKBOOK.md).
