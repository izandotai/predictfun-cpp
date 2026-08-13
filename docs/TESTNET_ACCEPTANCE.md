# BNB testnet acceptance harness

`predictfun_testnet_acceptance` closes the live-evidence gap without making a
testnet mutation easy to trigger accidentally. It is not linked into PMT and
does not call Predict's REST order API.

## Safety contract

- The only supported chain is BNB testnet, chain id `97`.
- `probe` is read-only and is the expected first command.
- `approve` operates on one minimal approval scope, never blanket approvals.
- A write requires all of `approve`, `--execute`, `--evidence`, and the exact
  owner/scope confirmation phrase printed by `probe`.
- The signer address must equal `--owner`.
- The private key is read with console echo disabled. It cannot be supplied by
  a command-line option, file, environment variable, or API key.
- A mutation is sent once. Timeout or lost response becomes an auditable
  unknown result; the tool never blindly resends it.
- The evidence file never contains the private key or raw signed transaction.

## Build

```sh
cmake --preset dev
cmake --build --preset dev --target predictfun_testnet_acceptance
```

The executable is built only when the explicit local-signer target is enabled.
Public/read-only package consumers do not link signer code.

## 1. Read-only probe

Use an EOA address that you control on BNB testnet:

```sh
./build/dev/predictfun_testnet_acceptance.exe probe \
  --owner 0xYOUR_ADDRESS \
  --scope trade-buy \
  --evidence runtime/predict-testnet-probe.jsonl
```

The probe validates remote chain id `97`, reads the testnet USDT balance, and
checks every step in the selected scope. It sends no transaction and prints
the exact confirmation phrase required for a later write.

Supported scopes:

- `trade-buy` - collateral allowance only;
- `trade-sell` - outcome-token operator approval;
- `split`, `merge`, `redeem`, `convert`;
- `--neg-risk` and `--yield-bearing` select the market contract track.

## 2. Explicit approval evidence

Only after inspecting the probe output:

```sh
./build/dev/predictfun_testnet_acceptance.exe approve \
  --owner 0xYOUR_ADDRESS \
  --scope trade-buy \
  --execute \
  --confirm "APPROVE PREDICT BNB TESTNET 97 0xYOUR_ADDRESS trade-buy:standard:regular" \
  --evidence runtime/predict-testnet-approval.jsonl
```

The program then prompts for the EOA key with terminal echo disabled. It
re-checks the selected approval, sends only missing steps, waits for receipts
at a 300 ms polling interval, and performs a postflight balance/approval read.
Already-satisfied scopes exit successfully without asking for a key or sending
a transaction.

There is intentionally no mainnet flag and no private-key flag.

## Evidence schema

The append-only JSONL schema is `predictfun.testnet.acceptance.v1`. Events are
timestamped in Unix milliseconds and include:

- session mode, owner, scope and chain id;
- chain validation;
- before/after USDT balance in exact base units;
- before/after approval state, token, spender and allowance;
- explicit write authorization;
- approval progress;
- deterministic transaction hash, receipt status, block number and gas used;
- final result and `blind_retry_performed: false`.

Evidence is append-only so an interrupted run retains every fact written before
the interruption. A lost submission response remains visible as an unconfirmed
transaction hash and must be reconciled rather than resent.
