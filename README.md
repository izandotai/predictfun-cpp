# predictfun-cpp

An independently layered C++20 client library for Predict.fun.

The current P0 milestone intentionally contains only:

- `predictfun::types`: strong market, decimal, price, and order-book types;
- `predictfun::codec`: bounded, strict JSON decoding for public market and
  order-book responses;
- deterministic YES-to-NO order-book derivation using integer ticks.

It intentionally contains no HTTP/WebSocket transport, API-key handling,
authentication, wallet, signer, RPC, or order submission code. Those layers
must be added as separate targets in later audited milestones.

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
cmake --preset dev -DPREDICTFUN_GLAZE_SOURCE_DIR=/verified/path/to/glaze
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

## Security boundary

P0 does not read environment files and does not know any credential names.
`p0_boundary` scans the production source boundary to prevent accidental
network, credential, signing, wallet, RPC, or order-submission dependencies.
