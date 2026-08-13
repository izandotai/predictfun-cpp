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

All calculations use exact 18-decimal integers. An insufficient book is a
valid `PARTIAL` quote rather than a fabricated complete fill. Quotes are
pre-fee; the market's `feeRateBps` is reported separately because actual fee
amounts belong to the resulting match event.

## Run

The host injects the API key through the process environment. The executable
does not read `.env` files, print the key, or persist it.

```sh
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_btc_liquidity_probe --both
PREDICT_FUN_API_KEY=... ./build/dev/predictfun_btc_liquidity_probe \
  --5m --jsonl ./runtime/btc-5m-liquidity.jsonl
```

The optional JSONL evidence contains only public market/book identifiers and
derived quotes under schema `predictfun.btc_liquidity.v1`.
