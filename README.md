# predictfun-cpp

An independently layered C++20 client library for Predict.fun.

The current SDK contains independently linkable authority layers:

- `predictfun::types`: strong market, decimal, price, and order-book types;
- `predictfun::codec`: bounded, strict JSON decoding for public market and
  order-book responses;
- `predictfun::net`: caller-executor asynchronous HTTPS with TLS hostname
  verification, deadlines, cancellation, bounded bodies, and redacted request
  summaries;
- `predictfun::public_rest`: typed, read-only markets, categories, order-book,
  and timeseries clients with global/endpoint rate limiting and bounded GET
  retries;
- `predictfun::public_wss`: typed, read-only public WebSocket subscriptions
  with exact heartbeat echoes, bounded frames and event queues, freshness
  tracking, reconnect/resubscribe, and explicit resynchronization states;
- `predictfun::auth`: bounded challenge/proof authentication with a
  caller-supplied asynchronous signer and move-only JWT ownership;
- `predictfun::private_rest` and `predictfun::private_wss`: authenticated
  account, activity, position, order and wallet-event reads;
- `predictfun::order` and optional `predictfun::local_signer`: integer-only
  order construction plus `izan-crypto` EIP-712 hashing and guarded,
  caller-controlled signing;
- `predictfun::analysis`: exact, read-only executable-depth estimates that
  distinguish complete fills from partial public liquidity;
- `predictfun::trading`: single-attempt create/remove mutations with explicit
  ambiguous-result semantics;
- `predictfun::lifecycle`: private-event plus REST reconciliation state;
- `predictfun::lifecycle`: also provides an append-only checksummed journal;
  journal-before-publish transitions are durably synced by default, and
  restart recovery quarantines every nonterminal order until the host supplies
  a complete authenticated REST snapshot;
- deterministic YES-to-NO order-book derivation using integer ticks.

P0 through P6 are implemented and pass the deterministic Debug and Release
test matrices. P7 production hardening is in progress; durable recovery,
shared REST budgets, reconnect-storm protection, property/adversarial tests,
fault injection, sanitizer CI, a codec fuzzer, installed-package consumers and
an explicitly gated BNB-testnet acceptance harness are present. Funded,
caller-authorized operation evidence and the final API audit remain. The
optional mainnet API key is supplied by the caller and is emitted
only as an `x-api-key` header; the SDK does not read environment files and
rejects credentials in request targets.

The complete SDK definition, module order and acceptance gates are tracked in
[`docs/FULL_SDK_ROADMAP.md`](docs/FULL_SDK_ROADMAP.md); current implementation
state is tracked in
[`docs/DEVELOPMENT_PROGRESS.md`](docs/DEVELOPMENT_PROGRESS.md). PMT integration
is intentionally deferred until every SDK gate passes.

## Build and test

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The normal SDK build never enables fuzzing. On a Clang/libFuzzer host, the
bounded codec target can be built and run explicitly:

```sh
cmake -S . -B build/fuzz -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DPREDICTFUN_BUILD_TESTS=ON -DPREDICTFUN_BUILD_TOOLS=OFF \
  -DPREDICTFUN_BUILD_FUZZERS=ON
cmake --build build/fuzz --target predictfun_codec_fuzz
./build/fuzz/predictfun_codec_fuzz -runs=2000 -max_len=4096
```

If an environment cannot validate GitHub TLS because its CMake installation
has no CA bundle, point configuration at a separately verified Glaze v7.8.4
source tree instead of disabling TLS verification:

```sh
cmake --preset dev \
  -DPREDICTFUN_GLAZE_SOURCE_DIR=/verified/path/to/glaze \
  -DPREDICTFUN_BOOST_SOURCE_DIR=/verified/path/to/boost-1.87 \
  -DPREDICTFUN_IZAN_CRYPTO_SOURCE_DIR=/verified/path/to/izan-crypto
```

`izan-crypto` is pinned to immutable commit
`8c6857d911da89a229e6a9911e984601e7cf15fa` and archive SHA-256
`E70B9BA33D93D98D052A73F88E3A56F340234E3BC8B3B91BE2E5D676DE682149`.
Read-only targets do not link its signing objects; order hashing links only
`izan::eip712`, while the separately selected local signer adds
`izan::secp256k1`.

## Numeric model

Public APIs never expose binary floating-point values for venue prices or
quantities. JSON decimals are parsed from their original lexical form into a
fixed decimal. Prices are then converted exactly to integer ticks at the
market's `decimalPrecision`.

Predict.fun stores the YES book. A NO view is derived without mutating it:

```text
no_bid_ticks = tick_scale - yes_ask_ticks
no_ask_ticks = tick_scale - yes_bid_ticks
```

## Read-only probe

The probe never links a wallet, signer, or order path. The host process may
provide a mainnet key through the environment for this diagnostic only:

```sh
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_read_only_probe --mainnet
```

The key value, query parameters, and response bodies are never printed.

The BTC liquidity probe deterministically locates the current continuous 5m
and 15m windows and measures exact UP/DOWN execution depth for $10, $25 and
$50 without linking a signer or mutation path:

```sh
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_btc_liquidity_probe --both
```

See [`docs/BTC_LIQUIDITY_PROBE.md`](docs/BTC_LIQUIDITY_PROBE.md) for output
semantics and optional credential-free JSONL evidence.

The public WebSocket probe takes an already-discovered market id and its
decimal precision. It subscribes read-only, waits for a fresh public order-book
message, and exits without linking any trading path:

```sh
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_public_wss_probe MARKET_ID PRECISION
```

## Crash recovery

`PersistentOrderTracker` records only typed lifecycle state: order hash/id,
exact requested and filled amounts, state, timestamps and a sanitized reason.
It does not persist credentials, signatures, HTTP bodies or raw venue events.
Each record has a length bound and CRC; a partial final record from a process
crash is removed on recovery, while corruption inside a completed record fails
closed. Nonterminal orders are never assumed failed and never blindly resent.

The recovery example inspects an existing journal without any network or key:

```sh
./build/dev/predictfun_recovery_example ./runtime/orders.journal
```

## Security boundary

`p0_boundary` keeps types/codec free of networking and credentials.
`p1_boundary` keeps public REST free of signing, wallets, RPC, environment-file
access, and order submission. Redirects are rejected rather than followed, and
secrets are forbidden in request targets and redacted request summaries.
`p2_boundary` applies the same restrictions to the public WebSocket layer and
also prevents private topics or order commands from entering the public API.
