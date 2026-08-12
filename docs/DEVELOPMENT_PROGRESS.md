# predictfun-cpp development progress

Updated: 2026-08-12

## Current status

| Milestone | State | Evidence |
|---|---|---|
| P0 types/codec boundary | complete | `codec`, `p0_boundary` tests |
| P1 public REST | complete | `public_rest`, `http_transport`, `p1_boundary` tests |
| P2 public WebSocket | complete | WSS codec/client/transport and `p2_boundary` tests |
| P3 authentication/private read | complete | private REST, wallet WSS, reconciliation gate and `p3_boundary` tests |
| P4 deterministic order builder | pending | official cross-SDK golden vectors required |
| P5 trading/reconciliation | pending | testnet mutation gate required |
| P6 BNB wallet operations | pending | testnet receipt/balance gate required |
| P7 hardening/release | pending | fresh-clone Debug/Release matrix required |

## Active P3 checklist

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

## Active P4 checklist

- [ ] Add integer-only amount math for LIMIT and MARKET orders.
- [ ] Add official BNB mainnet/testnet contract registry with provenance.
- [ ] Build canonical EIP-712 domain, order struct, digest and order hash.
- [ ] Add recoverable secp256k1 EOA signing behind a separately linkable target.
- [ ] Add Predict Account signature envelope support.
- [ ] Cross-check golden vectors byte-for-byte against the official SDK.

## Next implementation step

Implement deterministic order math and EIP-712 hashing with official SDK
golden vectors. This remains a pure local layer: no HTTP mutation or PMT
integration is introduced.
