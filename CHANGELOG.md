# Changelog

All notable changes to predictfun-cpp are documented here. The project follows
[Semantic Versioning](https://semver.org/); while the SDK is below 1.0, minor
versions may contain public API changes and patch versions are intended to be
compatible within the same minor line.

## [0.1.0] - 2026-08-15

Initial public SDK release.

### Added

- Exact decimal, tick, EVM and order-book types with deterministic YES/NO book
  derivation and executable-liquidity analysis.
- Bounded asynchronous public REST and WebSocket clients with TLS hostname
  verification, freshness tracking, rate budgets and reconnect protection.
- Caller-owned authentication, private REST and private wallet WebSocket
  layers with explicit reconciliation after stream generation changes.
- Integer-only order construction, EIP-712 hashing and separately linkable
  local signing backed by a pinned `izan-crypto` revision.
- Single-attempt trading mutations with explicit rejection and ambiguous-result
  semantics; no unsafe mutation retry is hidden inside the SDK.
- Durable lifecycle journal, restart quarantine and host-facing execution
  session with bounded multi-page reconciliation.
- BNB-chain approval, split, merge, convert and redeem operations, including
  caller-authorized testnet receipt and post-balance acceptance evidence.
- Installed CMake package, authority-layered API reference, compile-checked
  cookbook, offline examples and signer-free installation mode.
- Property, adversarial, fault-injection, transport and deterministic boundary
  tests, plus sanitizer/fuzzer configuration and an isolated release gate.

### Safety boundaries

- The SDK never reads `.env` files and never owns application credentials.
- Mainnet writes require an explicit caller-supplied API key, signer and
  transport invocation; examples do not enable them implicitly.
- Venue prices, quantities, fees and hashes never use binary floating point.
- PMT integration is intentionally outside this release.

[0.1.0]: https://github.com/izandotai/predictfun-cpp/releases/tag/v0.1.0
