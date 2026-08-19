# predictfun-cpp cookbook

These recipes preserve the SDK's authority boundaries. The source examples in
`examples/` are built with warnings-as-errors in normal CI and again against
the isolated installed package during `scripts/verify-release.sh`.

## 1. Install and consume an exact package version

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_predict_reader LANGUAGES CXX)

find_package(predictfun 0.1.0 EXACT CONFIG REQUIRED)
add_executable(my_predict_reader main.cpp)
target_compile_features(my_predict_reader PRIVATE cxx_std_20)
target_link_libraries(my_predict_reader PRIVATE
    predictfun::public_rest
    predictfun::analysis)
```

Build the SDK and install it into a caller-selected prefix:

```sh
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/predictfun \
  -DPREDICTFUN_BUILD_TESTS=OFF \
  -DPREDICTFUN_BUILD_TOOLS=OFF
cmake --build build/release
cmake --install build/release
```

Configure the consumer with `-DCMAKE_PREFIX_PATH=/opt/predictfun`.

## 2. Estimate executable depth without a network or credential

[`examples/exact_liquidity.cpp`](../examples/exact_liquidity.cpp) constructs a
small exact orderbook, prices a fixed collateral budget and measures an
immediate visible-book round trip. It never opens a socket and never links a
mutation target.

```sh
cmake --preset dev
cmake --build --preset dev --target predictfun_example_exact_liquidity
./build/dev/predictfun_example_exact_liquidity
```

Production checklist:

- pass asks from lowest to highest price;
- pass bids from highest to lowest price;
- treat `complete=false` as insufficient visible depth;
- keep venue fees separate from `book_loss_value_wei`;
- retain integer wei/tick values until presentation.

## 3. Read one BNB-testnet orderbook

[`examples/public_orderbook.cpp`](../examples/public_orderbook.cpp) accepts a
testnet market id and its declared decimal precision, performs one bounded
public REST read and derives the NO book from that same canonical YES snapshot.
It does not load a key, signer, wallet or trading target.

```sh
cmake --build --preset dev --target predictfun_example_public_orderbook
./build/dev/predictfun_example_public_orderbook MARKET_ID DECIMAL_PRECISION
```

The example deliberately requires explicit market metadata. Do not guess a
precision or mix books from different snapshots. For discovery, query open
markets first and use each returned market's `id` and `decimal_precision`.

## 4. Add a mainnet API key in the host, not the SDK

The SDK accepts a provider callback. Credential discovery belongs to the host:

```cpp
predictfun::public_rest::ClientOptions options;
options.environment = predictfun::Environment::bnb_mainnet;
options.api_key = [key = load_secret_in_the_host()] { return key; };
```

`load_secret_in_the_host()` is intentionally not supplied by predictfun-cpp.
The SDK never searches `.env` files. Keep the key out of URLs, trace ids,
command-line arguments and logs. If several clients share the same key, also
share one `net::RateLimiter` through their options.

## 5. Run a public stream safely

Create `public_wss::PublicWsClient` on the host executor, call `start` with
typed topics, and use the notification callback only to wake the consumer.
Drain events with `try_pop_event()` until empty. Monitor:

- `state()` for subscription/freshness state;
- `stats().generation` for reconnect boundaries;
- malformed/stale/overflow counters;
- reconnect-storm cooldowns.

Never treat an old generation's book as synchronized with a new stream. Fetch
or wait for a new authoritative snapshot before publishing executable depth.

## 6. Authenticate without transferring key ownership to the SDK

Implement `auth::MessageSigner` in the host or adapt an approved custody
service. `AuthClient::async_authenticate` asks that interface to sign the
bounded challenge and returns a move-only JWT. Feed API key/JWT providers into
private clients; do not serialize either secret.

If local signing is explicitly selected, link `predictfun::local_signer` and
move a `SecretString` into `LocalSigner::create`. The signer does not read disk
or environment variables. Adapt `LocalSigner::sign_personal_message` to
`auth::MessageSigner::async_sign_message` for the bounded authentication
challenge; do not use the order-only 32-byte helper for arbitrary text. A
read-only deployment should configure and install
with `PREDICTFUN_BUILD_LOCAL_SIGNER=OFF`.

## 7. Submit through the durable session

Follow [DURABLE_ORDER_SESSION.md](DURABLE_ORDER_SESSION.md). The minimum safe
sequence is:

1. build and sign one deterministic request;
2. open `DurableOrderSession` at a stable account/environment journal path;
3. call `async_submit_order` once;
4. distinguish acknowledged, rejected and ambiguous results;
5. forward private wallet events into the session;
6. after every private-stream generation change, call `async_reconcile` and
   keep host mutation decisions quarantined until it completes.

Do not call `TradingClient` as a fallback when the durable session reports an
ambiguous create. That would defeat the no-duplicate guarantee.

## 8. Recover after a crash

Inspect a journal offline:

```sh
./build/dev/predictfun_recovery_example ./runtime/orders.journal
```

Use a stable file per account and environment. A torn final record is expected
after some crashes and is handled safely; an interior checksum failure is a
hard stop. Recovery never proves venue state, so nonterminal entries remain
quarantined until authenticated REST reconciliation succeeds.

## 9. Execute a BNB operation only with an explicit gate

Chain writes require the optional signer target and operator authorization.
Use the chain-97 acceptance harness before integrating a host:

```sh
./build/dev/predictfun_testnet_acceptance --help
```

The harness binds owner, scope, operation and exact amount into its confirmation
phrase, performs `eth_call` preflight, sends once and reconciles the receipt.
Never substitute an unofficial faucet, arbitrary mint call or automatic secret
discovery. See [TESTNET_ACCEPTANCE.md](TESTNET_ACCEPTANCE.md).

## 10. Verify a release artifact

```sh
./scripts/verify-release.sh
```

The gate builds/tests an isolated full Release install, compiles and runs the
public and exact-liquidity cookbook consumers against `predictfun 0.1.0 EXACT`,
checks the explicit signer consumer, then repeats with signing disabled and
proves signer headers/targets are absent.
