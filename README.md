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
  order construction, EIP-712 hashing and caller-controlled signing;
- `predictfun::trading`: single-attempt create/remove mutations with explicit
  ambiguous-result semantics;
- `predictfun::lifecycle`: private-event plus REST reconciliation state;
- deterministic YES-to-NO order-book derivation using integer ticks.

P0 through P5 are implemented and pass the deterministic Debug test matrix.
BNB-chain wallet operations and production hardening remain in progress. The
optional mainnet API
key is supplied by the caller and is emitted
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

If an environment cannot validate GitHub TLS because its CMake installation
has no CA bundle, point configuration at a separately verified Glaze v7.8.4
source tree instead of disabling TLS verification:

```sh
cmake --preset dev \
  -DPREDICTFUN_GLAZE_SOURCE_DIR=/verified/path/to/glaze \
  -DPREDICTFUN_BOOST_SOURCE_DIR=/verified/path/to/boost-1.87
```

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

The public WebSocket probe takes an already-discovered market id and its
decimal precision. It subscribes read-only, waits for a fresh public order-book
message, and exits without linking any trading path:

```sh
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_public_wss_probe MARKET_ID PRECISION
```

## Security boundary

`p0_boundary` keeps types/codec free of networking and credentials.
`p1_boundary` keeps public REST free of signing, wallets, RPC, environment-file
access, and order submission. Redirects are rejected rather than followed, and
secrets are forbidden in request targets and redacted request summaries.
`p2_boundary` applies the same restrictions to the public WebSocket layer and
also prevents private topics or order commands from entering the public API.
