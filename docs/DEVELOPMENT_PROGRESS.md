# predictfun-cpp development progress

Updated: 2026-08-14

## 2026-08-14 - caller-authorized BNB-testnet position acceptance complete

- Closed the remaining P6 live-evidence gate without linking the harness into
  PMT. Every write was preceded by a successful owner-bound `eth_call`, sent
  exactly once, receipt-confirmed, and followed by fresh balance/approval
  reads. No blind retry occurred.
- Proved the standard track with split
  (`0x6534a35563a9db4ee65bee2f9b85cc9f78969e8058acd7d379c009d4ed59705c`),
  merge
  (`0xf2d65b3d08293bb20c3a32976344f221bb9d47d3d3953070a67693a607880e06`)
  and winning-outcome redeem
  (`0xf0f2e1f3b2c0bb8832595b1b7919acc476b90bf60809f351ad29caebc4910d9c`).
- Proved the negative-risk track with split
  (`0x1b8339a0ba6adb6697721b3d7b5b140f6abf22c28daa2b42369699e6aba9a7a0`),
  cross-market `convertPositions`
  (`0x72de530d59bec240923b6f4cc51ca71a8b95a996c070fead9e97df4ecab02d11`)
  and winning-outcome redeem
  (`0xe544d3d85eedc2c2952f9e9d649fd21269cb19fb804df4ae895504bb68f1fe24`).
  The convert burned one market-798 NO and minted one market-799 YES; the
  subsequent redeem restored collateral from `999000000000000000000` to
  `1000000000000000000000` base units. The losing market-798 YES remains as an
  expected worthless resolved-token residue.
- Added exact bounded ERC-20 approval amounts to the acceptance runner. A
  one-operation allowance is included in the confirmation phrase, accepted as
  sufficient for that operation, and visibly becomes missing after the
  contract consumes it. The default SDK policy remains exchange-compatible
  MaxInt256 when no exact amount is requested.
- Fixed an optimized-build-only C++ argument-evaluation hazard in transaction
  gas estimation by constructing RPC parameters before moving the transaction
  into its completion handler.
- Acceptance JSONL remains locally ignored operator evidence. Private keys,
  secret-file paths and raw signed transactions are absent from documentation,
  logs and version control.

## 2026-08-14 - durable host-facing execution session

- Added separately linkable `predictfun::execution`. Its durable session
  validates the signed request, derives the exact 18-decimal share quantity,
  and fsyncs the deterministic order hash before dispatching exactly one create
  mutation.
- Explicit venue/client rejection is now a terminal journal transition;
  transport/server ambiguity remains quarantined and is never automatically
  replayed.
- Private wallet events, book-removal notices and host snapshots share one
  Asio strand. A private-stream generation change first quarantines every
  nonterminal order, then consumes every bounded REST order page before
  releasing only records actually observed.
- Added deterministic tests for BUY/SELL share semantics, acknowledged,
  rejected and ambiguous submissions, invalid pre-dispatch requests, restart
  recovery and complete multi-page reconciliation. Added a static authority
  boundary plus an installed-package execution consumer.
- Added `docs/DURABLE_ORDER_SESSION.md` as the host integration contract. The
  layer owns no signer, wallet, environment reader, API credential or RPC
  authority.

## 2026-08-14 - official schema and lifecycle parity

- Re-audited every documented general-use REST route and all six public/private
  WebSocket topics. The durable matrix is in
  `docs/OFFICIAL_API_COVERAGE.md`; OAuth and schema-only objects are explicitly
  separated from the supported public surface.
- Extended market decoding with the complete documented lifecycle, visibility,
  resolution/oracle metadata, thresholds, boosts, external venue identifiers,
  rewards and embedded statistics.
- Extended categories with resolution provider, parent slug, category
  statistics and bounded team/variant payload preservation.
- Added `INVALIDATED` order semantics and fail-closed lifecycle handling.
  Sports/variant payloads remain bounded raw JSON so future venue fields are
  retained without leaking the wire JSON implementation into the public API.
- Added deterministic coverage for rich current-schema responses, all new
  lifecycle states, rewards, optional three-cent liquidity and bounded sports
  metadata. Clean Debug and Release matrices are both green at 31/31, and the
  installed public and signer package consumers both build and run.

## 2026-08-14 - documented REST surface completion

- Added bounded typed clients/codecs for `/v1/tags`, market statistics,
  nullable last sale, heterogeneous search results and public-address
  positions. Big integer identifiers and decimal amounts remain lossless.
- Extended categories with official image/tag metadata without exposing wire
  JSON types through the public API.
- Added authenticated referral assignment. It is dispatched at most once;
  transport, malformed-success and server ambiguity is returned with the
  `account.referral` reconciliation key and is never blindly replayed.
- Re-probed the isolated BNB-testnet acceptance wallet after the caller's
  faucet claim. Native gas remains funded, while the collateral registered by
  Predict at `0xb32171ecd878607ffc4f8fc0bcce6852bb3149e0` still has zero balance.
  No approval, split or other write was attempted.

## 2026-08-13 - long-running BTC liquidity collection hardening

- Added atomic, credential-free collector health status with last round,
  last successful evidence, transient error counters and sanitized reason.
- Continuous mode now survives transient category/order-book failures; a past
  error no longer makes every later successful round report process failure.
- Added graceful signal shutdown, quiet operation, parent-directory creation,
  and run/status/stop scripts backed by one fixed append-only journal.
- Extended the report with distinct-window counts and Q1-Q4 full-window
  coverage, preventing high-frequency rows from masquerading as independent
  evidence.
- The collector remains public/read-only and has no signer, wallet, order or
  mutation dependency.

## 2026-08-13 — exact BTC 5m/15m executable-liquidity measurement

- Added separately linkable `predictfun::analysis` with exact-integer fixed
  budget market-buy quotes: complete/partial, spend, shares, VWAP, worst price
  and consumed levels.
- Added a read-only mainnet probe that deterministically locates the current
  BTC 5m and 15m categories and measures both UP/DOWN at $10/$25/$50.
- Live mainnet verification found both current windows and successfully priced
  every requested budget. The evidence contains no API credential and clearly
  reports the market fee rate separately from pre-fee book execution.
- Added invalid/empty/insufficient/cross-level deterministic coverage and
  installed the new analysis target for downstream consumers.

## 2026-08-13 — P7 live testnet readiness gate

- Revalidated BNB testnet chain id 97, 0.3 tBNB native gas, registered
  collateral precision/balance, scoped approvals, open markets, orderbook and
  categories through live read-only probes.
- Audited the official Predict developer documentation plus the TypeScript and
  Python SDKs. They publish no supported test-collateral faucet or mint API.
- Added a deterministic acceptance readiness report that separates native gas,
  registered collateral and scoped approvals, persists the result in evidence,
  and identifies the next safe action without attempting a transaction.
- Status: gas and public data are READY; funded position operations are BLOCKED
  only by zero registered test collateral. No unofficial mutation path is used.

## Current status

## 2026-08-13 - persistent BTC liquidity evidence

- Extended the exact analysis boundary with descending-bid market-sell quotes
  and same-snapshot buy/sell round trips. Partial purchases count only actual
  collateral spent, never unavailable budget, in book-loss calculations.
- Added bounded finite or continuous sampling. Every round re-derives the
  current BTC 5m/15m slug and therefore crosses windows without stale reuse.
- Upgraded the public evidence schema to `predictfun.btc_liquidity.v2` with
  exact window phase, buy/exit completeness, recovered collateral and
  book-only loss.
- Added a credential-free report grouped by 5m/15m, normalized Q1-Q4 window
  phase, UP/DOWN and $10/$25/$50 budget.
- Preserved fee-rate metadata separately. Official Predict wallet events emit
  the realized fee amount and whether it is collateral or shares only after a
  successful transaction, so read-only book evidence does not fabricate a
  fee-adjusted fill.

| Milestone | State | Evidence |
|---|---|---|
| P0 types/codec boundary | complete | `codec`, `p0_boundary` tests |
| P1 public REST | complete | `public_rest`, `http_transport`, `p1_boundary` tests |
| P2 public WebSocket | complete | WSS codec/client/transport and `p2_boundary` tests |
| P3 authentication/private read | complete | private REST, wallet WSS, reconciliation gate and `p3_boundary` tests |
| P4 deterministic order builder | complete | official SDK/ethers golden vectors and local signer tests |
| P5 trading/reconciliation | implementation complete | `trading`, `lifecycle`, durable `execution`, match/order REST and authority-boundary tests |
| P6 BNB wallet operations | complete | deterministic gates plus caller-authorized standard and neg-risk split/merge/convert/redeem receipts and post-balances |
| P7 hardening/release | in progress | durable host execution/recovery, shared rate budgets, reconnect-storm protection, hostile-input testing and gated testnet acceptance harness implemented |

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
- [x] Prove balance transitions and receipts on caller-authorized testnet.

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
- [x] Capture caller-authorized BNB testnet receipts and post-operation balance
  transitions for split, merge, convert and redeem.

Current deterministic matrix: 33/33 Debug and 33/33 Release tests. The
installed public and explicitly opted-in signer consumers also build and run.
Official endpoint, WebSocket-topic and response-model coverage is tracked
separately in `docs/OFFICIAL_API_COVERAGE.md`.

Release verification also rebuilds from the pinned `izan-crypto` archive,
not a mutable checkout. A read-only build emits neither
`predictfun_local_signer` nor `izan_secp256k1`, while an installed-package
consumer must opt into the signer target explicitly.

An isolated BNB-testnet-only acceptance wallet exists in locally ignored
operator configuration outside version control. The final executable has
successfully captured live read-only chain-97 preflights for balances,
precision, approvals and a position operation. No key material or acceptance
evidence is stored in this repository.

The acceptance snapshots report native BNB in wei separately from the
registered Predict test collateral. This prevents a funded gas wallet from
being misdiagnosed as a funded trading wallet and applies to both approval and
position-operation probes. After the official faucet claim, a fresh live
read-only BNB testnet probe verified `299990301500000000` wei of native gas and
`1000000000000000000000` registered-collateral base units at 18 decimals. A
one-token standard split probe then bound the public condition and both outcome
token ids, constructed the exact calldata and passed `eth_call` without sending
a transaction. The probe identified only the minimal conditional-token
collateral allowance as missing.

The live acceptance gate is now closed for standard and negative-risk
split/merge/convert/redeem paths. Following explicit operator authorization,
the acceptance CLI may
read its BNB-testnet key from one explicitly named local, ignored env file. It
still refuses raw key arguments and process-environment keys, validates the
derived signer against `--owner`, and never exposes the path or secret in
evidence. This narrow harness-only exception does not weaken the SDK signer or
production transport authority boundary. No key material or live evidence is
committed.
