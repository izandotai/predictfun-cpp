# BTC 5m/15m liquidity probe

`predictfun_btc_liquidity_probe` is a mainnet, read-only diagnostic for the
continuous BTC Up/Down markets. It never links authentication, a signer,
trading mutations, a wallet, or chain operations.

## Market selection

The probe derives the current UTC window deterministically:

```text
btc-updown-5m-{floor(unix_time / 300) * 300}
btc-updown-15m-{floor(unix_time / 900) * 900}
```

It then requests the exact category and its order book. It does not scan pages,
guess from titles, select a future window, or reuse a stale market.

## Measurements

For both UP and DOWN, the probe reports:

- best bid and ask;
- whether $10, $25, and $50 can fill completely;
- executable collateral spend and shares;
- volume-weighted average execution price;
- worst price reached and number of levels consumed.
- whether the purchased shares can be sold back completely into the same
  snapshot, the recovered collateral, and the resulting book-only loss.

All calculations use exact 18-decimal integers. An insufficient book is a
valid `PARTIAL` quote rather than a fabricated complete fill. Unspent purchase
budget is never counted as a loss. The round trip is deliberately a
same-snapshot stress measurement; it is not a prediction of a later exit.

Quotes are pre-fee. The market's `feeRateBps` is recorded separately because
the official account event reports the realized fee only after a successful
transaction, including whether it was charged in collateral or shares. A
public order book alone cannot authoritatively manufacture that result.

## Run

The host injects the API key through the process environment. The executable
does not read `.env` files, print the key, or persist it.

```sh
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_btc_liquidity_probe --both
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_btc_liquidity_probe \
  --5m --jsonl ./runtime/btc-5m-liquidity.jsonl
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_btc_liquidity_probe \
  --both --samples 0 --interval-ms 5000 \
  --jsonl ./runtime/btc-liquidity-continuous.jsonl \
  --status-json ./runtime/btc-liquidity-status.json --quiet
```

`--samples 0` runs until interrupted. Finite sample counts and intervals of at
least 1000 ms are also supported. Every round re-derives the current UTC slug,
so a long-running process crosses 5m/15m windows without retaining a stale
market. Rows are flushed immediately, making an interrupted journal usable.
Transient REST/category/order-book failures are counted in the health status
but do not poison later successful rounds or stop continuous collection.
`--status-json` publishes an atomic, credential-free heartbeat with the last
round/success timestamps and error counters. `--quiet` suppresses repetitive
human output without changing evidence.

The optional JSONL evidence contains only public market/book identifiers and
derived quotes under schema `predictfun.btc_liquidity.v2`. It also records the
window start, duration, elapsed time and remaining time.

## Aggregate report

The credential-free report groups evidence by interval, outcome, budget and
normalized window quarter (`Q1` through `Q4`):

```sh
./build/dev/predictfun_btc_liquidity_report \
  ./runtime/btc-liquidity-continuous.jsonl
```

It reports purchase and immediate-round-trip completion rates, mean VWAP,
mean worst price, and mean/maximum book-only loss. Reported market fee rates
remain visible but are not deducted from the book-only metric.
It also reports distinct-window coverage per Q1-Q4 and the count of windows
observed in all four quarters; row count alone is never presented as complete
coverage.

## Long-running collector scripts

The scripts use one fixed append-only evidence path across restarts. They read
only `PREDICT_FUN_API_KEY` from the environment (or the explicitly selected
env file), and never source or execute the env file as shell code.

```sh
./scripts/run-btc-liquidity-collector.sh
./scripts/status-btc-liquidity-collector.sh
./scripts/stop-btc-liquidity-collector.sh
```

Use `--foreground` on the run script for an attached console. The default
background mode records PID, atomic status, stdout/stderr and evidence under
`runtime/btc-liquidity/`. It executes a runtime copy of the release binary, so
Windows does not lock the build artifact while collection is running. Stopping
preserves the evidence journal.
