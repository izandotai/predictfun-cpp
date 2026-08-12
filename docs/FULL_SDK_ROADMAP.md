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
- TLS-verified HTTP transport with deadlines, cancellation and redaction.
- Public WebSocket subscriptions with heartbeat, freshness, reconnect and
  resynchronization semantics.

### P3: authentication and private read foundation — in progress

- General GET/POST transport without secrets in targets or diagnostics.
- Validated EVM addresses, move-only secret storage and JWT redaction.
- `/v1/auth/message` and `/v1/auth` codecs/client.
- Caller-supplied message-signer abstraction for EOA/Predict Account flows.
- Authenticated request headers and typed account/activity/position/order reads.
- Private `predictWalletEvents/{jwt}` subscription and bounded event codec.

Gate: deterministic mocked lifecycle plus a read-only testnet probe; no private
key is ever accepted by REST/WSS modules or printed by any error path.

### P4: deterministic order construction

- Exact limit/market/slippage amount math using integers only.
- Contract order, strategy, side, STP and fee types.
- EIP-712 domain/types/value generation for all market variants.
- Typed-data hash, EOA signature and Predict Account signature envelopes.
- Golden vectors cross-checked against official TypeScript and Python SDKs.

Gate: hashes, signatures and encoded request bodies match official fixtures
byte-for-byte on BNB mainnet and testnet.

### P5: trading API and reconciliation

- Create limit/market orders; query one/all orders and matches.
- Cancel selected hashes or remove eligible orders from the book.
- Idempotency keys, client order correlation and bounded retry policy.
- Private-event state machine and REST reconciliation after disconnects.
- Ambiguous-submit quarantine: never blindly resend an order whose outcome is
  unknown; look up by deterministic hash before deciding.

Gate: testnet create/partial-fill/fill/cancel/reject/reconnect scenarios leave
the local state identical to venue state.

### P6: BNB-chain wallet operations

- Typed JSON-RPC transport, chain-id validation and receipt tracking.
- Official contract addresses/ABIs with versioned provenance.
- Pure scoped approval plans plus check/run/progress APIs.
- USDT balance/allowance, ERC-1155 balances and approvals.
- Split, merge, convert and redeem for standard/neg-risk/yield-bearing markets.
- EOA and Predict Account transaction routing.

Gate: testnet receipts and post-transaction balances prove every operation;
wrong-chain, revert, replacement and timeout paths are covered.

### P7: production hardening and release

- Persistent reconciliation journal and restart recovery example.
- Rate-limit budgets per endpoint and reconnect-storm protection.
- Fuzz/property tests for codecs, amount math and state machines.
- ASan/UBSan/TSan where supported; Windows static-PE verification.
- API reference, cookbooks and semver/package installation test.

Gate: clean Debug/Release builds and complete test matrix from a fresh clone.

## Non-negotiable safety properties

- No SDK module reads `.env` files. Secrets are injected by the host.
- No secret appears in a URL, exception, trace id or sanitized summary.
- Mainnet mutation examples are disabled unless the caller explicitly opts in.
- No floating point is used for venue prices, quantities, fees or order hashes.
- A transport timeout is not evidence that a mutation failed.
- Public, authenticated-read, signing and mutation layers remain separately
  linkable so consumers can keep the smallest possible authority surface.
