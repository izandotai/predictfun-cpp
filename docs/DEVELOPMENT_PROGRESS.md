# predictfun-cpp development progress

Updated: 2026-08-12

## Current status

| Milestone | State | Evidence |
|---|---|---|
| P0 types/codec boundary | complete | `codec`, `p0_boundary` tests |
| P1 public REST | complete | `public_rest`, `http_transport`, `p1_boundary` tests |
| P2 public WebSocket | complete | WSS codec/client/transport and `p2_boundary` tests |
| P3 authentication/private read | complete | private REST, wallet WSS, reconciliation gate and `p3_boundary` tests |
| P4 deterministic order builder | complete | official SDK/ethers golden vectors and local signer tests |
| P5 trading/reconciliation | implementation complete | `trading`, `lifecycle`, match/order REST and `p5_boundary` tests |
| P6 BNB wallet operations | implementation complete | deterministic transaction/approval gate complete; live testnet evidence pending |
| P7 hardening/release | in progress | durable recovery, shared rate budgets and reconnect-storm protection implemented |

## Completed P3 checklist

- [x] Audit present targets and compare them with the official endpoint/SDK
  surface.
- [x] Define what “complete SDK” means and make PMT integration explicitly
  downstream.
- [x] Generalize HTTP transport to bounded GET/POST requests while retaining
  redirect rejection and redacted summaries.
- [x] Add EVM address and move-only secret/JWT types.
- [x] Add bounded auth request/response codecs.
- [x] Add `/v1/auth/message` + `/v1/auth` asynchronous client.
- [x] Add external signer abstraction; SDK auth code must never own a private
  key.
- [x] Add authenticated REST base and account/activity/order/position read endpoints.
- [x] Add private wallet-event WebSocket codec/client.
- [x] Require REST reconciliation after every wallet-stream generation/reconnect.
- [x] Extend P3 secret/authority boundary across every private-read module.
- [x] Compile and pass deterministic private REST/WSS acceptance tests.

## Completed P4 checklist

- [x] Add integer-only amount math for LIMIT and MARKET orders.
- [x] Add official BNB mainnet/testnet contract registry with provenance.
- [x] Build canonical EIP-712 domain, order struct, digest and order hash.
- [x] Add recoverable secp256k1 EOA signing behind a separately linkable target.
- [x] Add Predict Account signature envelope support.
- [x] Cross-check golden vectors byte-for-byte against the official SDK.

P4 provenance and constraints:

- Venue behavior was audited against Predict's official SDK at commit
  `5ff2b4a1c54cbbeb0fd661e17246cd12a9af8486`.
- Mainnet/testnet domain and order hashes were generated independently with
  `ethers`, then frozen as C++ golden vectors.
- The local signer pins Bitcoin Core `libsecp256k1` v0.7.1 by SHA-256 and is a
  separate CMake target. Public/read-only consumers do not link signing code.
- The signer accepts only an explicit move-only in-memory secret. It does not
  read `.env`, files or process environment variables.
- Market order construction is intentionally stricter than the official
  helper: insufficient visible depth is rejected instead of silently building
  an order from a partial book.

## Completed P5 checklist

- [x] Add typed create-order and remove-order mutation codecs and clients.
- [x] Keep mutations single-attempt; never infer failure from a timeout.
- [x] Use the deterministic order hash as the correlation/idempotency key.
- [x] Add authenticated order-by-hash lookup for submit reconciliation.
- [x] Add typed match-event query/decode support.
- [x] Track accepted/open/partial/filled/removed/cancelled/expired/rejected,
  transaction and ambiguous states.
- [x] Require a complete REST snapshot after every private-stream generation.
- [x] Treat order-book removal as nonterminal because it is not an on-chain
  cancellation.
- [x] Compile and pass the complete 20-test Debug matrix.

## P6 progress

- [x] Add separately linkable `predictfun::chain` target.
- [x] Add bounded JSON-RPC reads, chain-id fail-closed validation and receipt
  decoding.
- [x] Add USDT/ERC-1155 balance and allowance queries.
- [x] Add minimal, operation-scoped approval plans and approval transaction
  builders for all standard, negative-risk and yield-bearing tracks.
- [x] Add byte-exact ABI builders for split, merge, redeem, convert and
  exchange `cancelOrders`, independently checked against Foundry `cast`.
- [x] Add explicit EOA/Predict Account routing; Predict Account calls are
  wrapped through Kernel `execute(bytes32,bytes)`.
- [x] Add gas/nonce estimation, raw transaction submission and receipt waiter.
- [x] Add optional local EIP-155 transaction signing plus external signer
  workflow.
- [x] Add approval check/run/progress orchestration with the official
  `MaxInt256` allowance threshold, deduplication and ordered progress.
- [x] Preserve deterministic transaction bytes/hash across accepted and
  ambiguous submissions; never blind-retry a response-lost mutation.
- [x] Reconcile submission outcome through bounded receipt polling and retain
  an explicit `outcome_unknown` result when confirmation cannot be proved.
- [ ] Prove balance transitions and receipts on caller-authorized testnet.

## Next implementation step

P7 durable recovery is implemented without integrating PMT:

- [x] Append-only, bounded, checksummed lifecycle journal.
- [x] Transactional journal-before-publish tracker mutations.
- [x] Torn-tail repair and fail-closed interior corruption handling.
- [x] Restart quarantine for all nonterminal/ambiguous orders.
- [x] Credential-free recovery example.
- [x] Shared global/endpoint rate budgets, `Retry-After` parsing and cooldown
  propagation across REST/auth/trading clients.
- [x] Public/private WebSocket exponential backoff, jitter, stable-session
  reset semantics and reconnect-storm circuit breaker.
- [ ] Fuzz/property tests and sanitizer jobs.
- [ ] Installed-package consumer matrix.
- [ ] Explicitly gated testnet operation harness.

Next: complete property/fuzz tests and sanitizer/fault-injection jobs.
