# predictfun-cpp development progress

Updated: 2026-08-13

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
| P7 hardening/release | in progress | durable recovery, shared rate budgets, reconnect-storm protection, hostile-input testing and gated testnet acceptance harness implemented |

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
- EIP-712, Keccak, U256 ABI words and recoverable secp256k1 signing share the
  audited `izan-crypto` boundary pinned at immutable commit
  `8c6857d911da89a229e6a9911e984601e7cf15fa` and archive SHA-256
  `E70B9BA33D93D98D052A73F88E3A56F340234E3BC8B3B91BE2E5D676DE682149`.
  Predict-specific order schemas remain in this SDK; generic cryptography is
  not duplicated here.
- The local signer is still a separate CMake target. Its raw scalar lives in
  libsodium guarded memory, is protected while idle and is wiped on
  destruction. Public/read-only consumers do not link signing code.
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
- [x] Deterministic property tests for exact amount math, prices and lifecycle
  safety invariants.
- [x] Hostile/mutated codec corpus plus a Clang/libFuzzer target covering all
  public, private, wallet, trading and authentication decoders.
- [x] Async fault injection proving duplicate transport completions and late
  post-cancellation responses cannot complete a public REST operation twice.
- [x] Manually triggered CI job configured to run the full matrix under
  ASan/UBSan and a deterministic libFuzzer smoke run.
- [x] Installed-package consumer matrix for public/read-only and explicit
  local-signer consumers.
- [x] Explicitly gated testnet scoped-approval harness with a default read-only
  probe, exact owner/scope confirmation, interactive secret input and
  append-only receipt/balance/approval evidence.
- [x] Decode public operation metadata (`conditionId`, `questionIndex` and
  `negRiskOnChainId`) without exposing wire-only types.
- [x] Extend the gated harness over split, merge, redeem and convert with exact
  operation-bound confirmation, balance/token evidence, mandatory `eth_call`
  preflight and a one-submission/no-blind-retry execution path.
- [ ] Capture caller-authorized BNB testnet receipts and post-operation balance
  transitions for split, merge, convert and redeem.

Current deterministic matrix: 29 Debug/Release tests.

Release verification also rebuilds from the pinned `izan-crypto` archive,
not a mutable checkout. A read-only build emits neither
`predictfun_local_signer` nor `izan_secp256k1`, while an installed-package
consumer must opt into the signer target explicitly.

An isolated BNB-testnet-only acceptance wallet now exists outside the source
tree in a Windows DPAPI-protected keystore. The final Debug executable has
successfully captured a live read-only chain-97 preflight: registered
collateral and its on-chain 18-decimal precision were read, the zero balance
was preserved, and the missing minimal trade-buy allowance was identified.
No key material or acceptance evidence is stored in this repository.

The acceptance snapshots now report native BNB in wei separately from the
registered Predict test collateral. This prevents a funded gas wallet from
being misdiagnosed as a funded trading wallet and applies to both approval and
position-operation probes. A fresh read-only BNB testnet probe verified
`300000000000000000` wei of native gas while the registered collateral remains
zero; the remaining acceptance dependency is obtaining test collateral, not
gas funding.

Next: obtain the deployed Predict test collateral for the isolated address,
then capture funded split/merge/convert/redeem receipts and post-operation
balance transitions without changing the SDK's authority boundary. Native BNB
gas is already funded and independently verified.
