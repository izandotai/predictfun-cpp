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
  --jsonl ./runtime/btc-liquidity-continuous.jsonl
```

`--samples 0` runs until interrupted. Finite sample counts and intervals of at
least 1000 ms are also supported. Every round re-derives the current UTC slug,
so a long-running process crosses 5m/15m windows without retaining a stale
market. Rows are flushed immediately, making an interrupted journal usable.

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
