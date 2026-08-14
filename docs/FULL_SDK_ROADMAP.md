# predictfun-cpp complete SDK roadmap

## Definition of complete

`predictfun-cpp` is complete only when an application can use its public C++
API to perform the full Predict.fun lifecycle without importing the official
TypeScript/Python SDK or reimplementing venue rules:

1. discover markets and consume public REST/WebSocket data;
2. authenticate an EOA or Predict Account and consume the private wallet feed;
3. read the connected account, activity, positions, orders, matches and fees;
4. calculate exact limit/market amounts, construct EIP-712 data, sign, hash,
   submit and cancel orders;
5. distinguish accepted, rejected, pending, partially filled, filled, cancelled,
   expired and transaction-final states and reconcile ambiguous submissions;
6. inspect and run scoped BNB-chain approvals, query USDT/position balances,
   and split, merge, convert and redeem positions;
7. operate on mainnet or testnet with bounded resources, cancellation,
   deadlines, redacted diagnostics and deterministic recovery;
8. ship examples, installed CMake targets, fixtures, unit/integration tests,
   fault injection, sanitizers and an independently reproducible release.

PMT integration is deliberately outside this roadmap. It starts only after all
SDK acceptance gates below pass.

## Milestones and acceptance gates

### P0-P2: public read-only foundation — complete

- Strong fixed-decimal market/order-book types.
- Bounded public REST codecs and clients.
- Complete documented discovery reads: tags, per-market statistics, nullable
  last sale, heterogeneous search results and public-address positions.
- TLS-verified HTTP transport with deadlines, cancellation and redaction.
- Public WebSocket subscriptions with heartbeat, freshness, reconnect and
  resynchronization semantics.
- Complete documented market/category lifecycle and metadata decoding,
  including rewards, embedded statistics and bounded forward-compatible
  sports/variant JSON payloads.
- Audited endpoint/topic inventory in `docs/OFFICIAL_API_COVERAGE.md`; OAuth and
  schema-only objects without public routes are intentionally not invented.

### P3: authentication and private read foundation — complete

- General GET/POST transport without secrets in targets or diagnostics.
- Validated EVM addresses, move-only secret storage and JWT redaction.
- `/v1/auth/message` and `/v1/auth` codecs/client.
- Caller-supplied message-signer abstraction for EOA/Predict Account flows.
- Authenticated request headers and typed account/activity/position/order reads.
- Single-attempt referral assignment; transport/server ambiguity is surfaced
  and must be reconciled through the account read rather than replayed.
- Private `predictWalletEvents/{jwt}` subscription and bounded event codec.

Gate: deterministic mocked lifecycle plus a read-only testnet probe; no private
key is ever accepted by REST/WSS modules or printed by any error path.

### P4: deterministic order construction — complete

- Exact limit/market/slippage amount math using integers only.
- Contract order, strategy, side, STP and fee types.
- EIP-712 domain/types/value generation for all market variants.
- Typed-data hash, EOA signature and Predict Account signature envelopes.
- Golden vectors cross-checked against official TypeScript and Python SDKs.

Gate: hashes, signatures and encoded request bodies match official fixtures
byte-for-byte on BNB mainnet and testnet.

Implemented with a separately linkable `predictfun::local_signer`; callers may
instead supply an external signer. The SDK never discovers or loads keys.

### P5: trading API and reconciliation - implementation complete

- Create limit/market orders; query one/all orders and matches.
- Cancel selected hashes or remove eligible orders from the book.
- Deterministic order hashes as client correlation/idempotency keys; mutation
  transport is single-attempt and never performs a blind retry.
- Private-event state machine and REST reconciliation after disconnects.
- Ambiguous-submit quarantine: never blindly resend an order whose outcome is
  unknown; look up by deterministic hash before deciding.

The deterministic implementation gate is complete: create/remove codecs,
single-order lookup, match-event reads, private-event transitions, disconnect
reconciliation and ambiguous-submit quarantine pass the Debug suite. The live
testnet acceptance scenario remains part of the final release matrix because
it requires an explicitly funded caller-owned account.

P7 now also supplies `predictfun::execution::DurableOrderSession`, the safe
host composition of these P5 primitives. It writes the deterministic hash to
the checksummed lifecycle journal before the single create attempt, persists
known rejection and ambiguous outcomes, serializes private-event updates, and
requires a bounded complete multi-page REST order snapshot after every private
WebSocket generation change. This removes submit-before-journal and blind
replay hazards without importing a signer, wallet, key loader or RPC path.

### P6: BNB-chain wallet operations - complete

- Typed JSON-RPC transport, chain-id validation and receipt tracking.
- Official contract addresses/ABIs with versioned provenance.
- Pure scoped approval plans plus check/run/progress APIs.
- USDT balance/allowance, ERC-1155 balances and approvals.
- Split, merge, convert and redeem for standard/neg-risk/yield-bearing markets.
- EOA and Predict Account transaction routing.

Gate: testnet receipts and post-transaction balances prove every operation;
wrong-chain, revert, replacement and timeout paths are covered.

The deterministic implementation gate is complete: chain validation,
balance/allowance reads, operation-scoped approval checks/runs, EIP-155 legacy
transaction signing, gas/nonce population, raw submission, ambiguous-response
reconciliation, receipt waiting and EOA/Predict Account routing pass the local
Debug suite. Caller-authorized BNB-testnet receipts and post-balance evidence
now cover standard split/merge/redeem and negative-risk
split/convert/redeem. The convert proof burns the selected market NO, mints the
corresponding YES in the other category market, and then redeems that winning
YES to restore the original collateral amount.

The P7 acceptance harness now provides BNB-testnet-only, default-read-only
approval and position-operation probes plus exact owner/scope/operation-gated
approval and split/merge/redeem/convert runners. Position probes bind public
condition/category identifiers, exact amounts and token-balance evidence into
the confirmation phrase and require a successful `eth_call` before any write.
The harness accepts secrets from an interactive hidden prompt or an explicitly
named, operator-authorized local secret file that is confined to the chain-97
acceptance executable. It records receipts and before/after balances/approvals
and never blind-retries an ambiguous submission. Raw key arguments, automatic
secret discovery and SDK-level file loading remain forbidden.

The acceptance wallet is deliberately kept outside the repository. Live
read-only probes have validated BNB testnet chain id 97, funded native BNB,
`1000000000000000000000` registered test-collateral base units at 18 decimals,
and current market/orderbook/category REST reads. The completed acceptance run
used exact amount-bound ERC-20 allowances where possible, mandatory `eth_call`
preflight, one submission per mutation, receipt reconciliation and fresh
postflight balances. Locally ignored evidence retains the audit trail without
committing secrets or operator artifacts. No unofficial faucet, arbitrary mint
call or embedded credential was introduced.

P7 also exposes exact read-only executable-liquidity analysis as a separately
linkable target. Its BTC 5m/15m probe selects the current epoch-derived market,
derives the DOWN book from the canonical UP book, and reports complete versus
partial $10/$25/$50 fills with integer VWAP, worst price and depth consumed.
It now also supports persistent cross-window sampling, exact same-snapshot
sell-back stress, normalized window-phase evidence and a credential-free
grouped report. This establishes a reusable, non-authoritative integration
boundary for PMT without importing credentials, signing or mutation authority.

### P7: production hardening and release

- Persistent reconciliation journal and restart recovery example.
- Shared global/endpoint rate-limit budgets with server-directed cooldowns.
- WebSocket exponential backoff, jitter and reconnect-storm protection that
  resets only after a fully live/reconciled session.
- Fuzz/property tests for codecs, amount math and state machines.
- ASan/UBSan/TSan where supported; Windows static-PE verification.
- API reference, cookbooks and semver/package installation test.
- Host-facing durable execution session and installed-package consumer test.

Gate: clean Debug/Release builds and complete test matrix from a fresh clone.

## Non-negotiable safety properties

- No SDK module reads `.env` files. Secrets are injected by the host.
- No secret appears in a URL, exception, trace id or sanitized summary.
- Mainnet mutation examples are disabled unless the caller explicitly opts in.
- No floating point is used for venue prices, quantities, fees or order hashes.
- A transport timeout is not evidence that a mutation failed.
- Public, authenticated-read, signing and mutation layers remain separately
  linkable so consumers can keep the smallest possible authority surface.
