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
| P5 trading/reconciliation | pending | testnet mutation gate required |
| P6 BNB wallet operations | pending | testnet receipt/balance gate required |
| P7 hardening/release | pending | fresh-clone Debug/Release matrix required |

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

## Next implementation step

Implement P5 mutation codecs and the trading state machine: create/cancel,
deterministic order correlation, bounded non-blind retry and ambiguous-submit
quarantine. This remains inside predictfun-cpp; no PMT integration is
introduced.
