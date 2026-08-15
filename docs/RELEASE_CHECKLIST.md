# Release checklist

This is the publication gate for `predictfun-cpp`. A tag is created only from a
clean `main` commit after every required item below passes.

## Scope and version

- [x] `project(predictfun_cpp VERSION 0.1.0)` is the single package version.
- [x] Installed consumers require `find_package(predictfun 0.1.0 EXACT)`.
- [x] Pre-1.0 compatibility is limited to the same minor line.
- [x] `CHANGELOG.md` describes the public surface and safety boundaries.
- [x] `LICENSE`, `README.md` and `CHANGELOG.md` are installed with the package.
- [x] PMT integration is outside the release and is not linked into the SDK.

## Authority and secret boundaries

- [x] `.env.local` is ignored and no tracked credential/private key exists.
- [x] Public reads, authenticated reads, order hashing, signing, trading and
      chain mutation remain separately linkable authority layers.
- [x] The signer-free install exports no local-signer header, library or CMake
      target.
- [x] Mutating calls remain single-attempt and surface ambiguous results.
- [x] Mainnet credentials and signers are supplied explicitly by the host.

## Reproducible evidence

- [x] Local warnings-as-errors Debug matrix passes 34/34 tests.
- [x] Isolated Release matrix passes 34/34 tests.
- [x] Full installed package passes public, exact-liquidity and signer
      consumers.
- [x] Signer-free installed package passes public/exact consumers and artifact
      inspection.
- [x] Installed release contains the license and release documentation.
- [x] `main`, `origin/main` and the annotated `v0.1.0` tag resolve to the same
      commit.

The first five evidence items are enforced by `scripts/verify-release.sh`
except for the local Debug matrix, which is run separately. GitHub Actions is
intentionally manual (`workflow_dispatch`) and runs the same isolated gate on
Ubuntu and Windows when requested.

## Known release boundaries

- This is a C++20 static-library SDK, not a hosted service or trading bot.
- Runtime availability and venue behavior remain external dependencies.
- The SDK provides deterministic safety mechanisms; it does not promise trade
  execution, liquidity, profitability or mainnet acceptance.
- BNB testnet acceptance receipts prove the implemented transaction paths but
  do not authorize future writes or replace caller-side risk controls.
