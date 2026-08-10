# predictfun-cpp

An independently layered C++20 client library for Predict.fun.

The current P1 milestone contains four independently linkable layers:

- `predictfun::types`: strong market, decimal, price, and order-book types;
- `predictfun::codec`: bounded, strict JSON decoding for public market and
  order-book responses;
- `predictfun::net`: caller-executor asynchronous HTTPS with TLS hostname
  verification, deadlines, cancellation, bounded bodies, and redacted request
  summaries;
- `predictfun::public_rest`: typed, read-only markets, categories, order-book,
  and timeseries clients with global/endpoint rate limiting and bounded GET
  retries;
- deterministic YES-to-NO order-book derivation using integer ticks.

P1 has no WebSocket, wallet, signer, RPC, JWT, or order submission code. The
optional mainnet API key is supplied by the caller and is emitted only as an
`x-api-key` header; the SDK does not read environment files.

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

## Security boundary

`p0_boundary` keeps types/codec free of networking and credentials.
`p1_boundary` keeps public REST free of signing, wallets, RPC, environment-file
access, and order submission. Redirects are rejected rather than followed, and
secrets are forbidden in request targets and redacted request summaries.
