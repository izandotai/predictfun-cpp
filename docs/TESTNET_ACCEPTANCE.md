# BNB testnet acceptance harness

`predictfun_testnet_acceptance` closes the live-evidence gap without making a
testnet mutation easy to trigger accidentally. It is not linked into PMT and
does not call Predict's REST order API.

## Safety contract

- The only supported chain is BNB testnet, chain id `97`.
- `probe` and `position-probe` are read-only and are the expected first
  commands for approvals and position operations respectively.
- `approve` operates on one minimal approval scope, never blanket approvals.
- An approval write requires all of `approve`, `--execute`, `--evidence`, and
  the exact owner/scope confirmation phrase printed by `probe`.
- A position write requires all of `position-execute`, `--execute`,
  `--evidence`, and the exact operation/owner/condition/amount/token-evidence
  confirmation phrase printed by `position-probe`.
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

The probe validates remote chain id `97`, reads the registered testnet
collateral balance, and checks every step in the selected scope. It sends no
transaction and prints the exact confirmation phrase required for a later
write.

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

## 3. Read-only position-operation preflight

Market and category responses expose the public on-chain identifiers needed by
the transaction builders:

- `Market::condition_id` and `Market::question_index`;
- `Category::neg_risk_on_chain_id` for negative-risk conversion.

Use those identifiers together with the outcome token ids returned by the
market API. For example, a standard split probe is:

```sh
./build/dev/predictfun_testnet_acceptance.exe position-probe \
  --owner 0xYOUR_ADDRESS \
  --operation split \
  --condition-id 0xCONDITION_ID \
  --amount 1000000000000000000 \
  --token-id YES_TOKEN_ID \
  --token-id NO_TOKEN_ID \
  --evidence runtime/predict-testnet-split-probe.jsonl
```

The amount is expressed in exact registered-collateral base units. The current
BNB testnet collateral reports 18 decimals on chain, so the example is one
whole test token. Acceptance code must read the deployed token's `decimals()`
instead of assuming mainnet-style USDT precision. The probe validates chain id
`97`, constructs the exact transaction through `predictfun::chain`, records
the calldata hash, reads before-balances and scoped approvals, and performs an
`eth_call` from the owner. It sends no transaction. A failed simulation is a
hard stop and does not print a write authorization phrase.

Operation-specific arguments:

- `split`: `--condition-id`, `--amount`, and exactly two `--token-id` values;
- `merge`: `--condition-id`, `--amount`, and exactly two `--token-id` values;
- `redeem`: `--condition-id` and one or two `--token-id` values; add
  `--neg-risk` for the negative-risk path;
- `convert`: `--neg-risk`, `--neg-risk-on-chain-id`, `--index-set`,
  `--amount`, and at least one evidence `--token-id`;
- `--yield-bearing` selects the yield-bearing collateral track where the
  operation supports it.

Token ids bind the confirmation phrase and the before/after balance evidence.
They are deliberately not inferred from an untrusted command response.

## 4. Explicit position-operation execution

First satisfy the operation's minimal approval scope with `probe` and
`approve`. Then repeat the validated position arguments and paste the exact
phrase emitted by `position-probe`:

```sh
./build/dev/predictfun_testnet_acceptance.exe position-execute \
  --owner 0xYOUR_ADDRESS \
  --operation split \
  --condition-id 0xCONDITION_ID \
  --amount 1000000000000000000 \
  --token-id YES_TOKEN_ID \
  --token-id NO_TOKEN_ID \
  --execute \
  --confirm "EXACT PHRASE PRINTED BY POSITION-PROBE" \
  --evidence runtime/predict-testnet-split-execution.jsonl
```

The tool revalidates the chain, balances, approvals and simulation before it
asks for the key through the hidden console prompt. It then submits exactly one
transaction, waits for its receipt, and records postflight balances. A lost or
ambiguous response is never retried automatically.

## Evidence schema

The append-only JSONL schema is `predictfun.testnet.acceptance.v1`. Events are
timestamped in Unix milliseconds and include:

- session mode, owner, scope and chain id;
- chain validation;
- before/after registered-collateral balance in exact base units;
- before/after approval state, token, spender and allowance;
- explicit write authorization;
- approval progress;
- position operation, public identifiers, exact amount/index set, evidence
  token ids, target/value/calldata hash and `eth_call` preflight result;
- deterministic transaction hash, receipt status, block number and gas used;
- final result and `blind_retry_performed: false`.

Evidence is append-only so an interrupted run retains every fact written before
the interruption. A lost submission response remains visible as an unconfirmed
transaction hash and must be reconciled rather than resent.
