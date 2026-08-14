# BNB testnet acceptance harness

`predictfun_testnet_acceptance` closes the live-evidence gap without making a
testnet mutation easy to trigger accidentally. It is not linked into PMT and
does not call Predict's REST order API.

## Safety contract

- The only supported chain is BNB testnet, chain id `97`.
- `probe` and `position-probe` are read-only and are the expected first
  commands for approvals and position operations respectively.
- `approve` operates on one minimal approval scope, never blanket approvals.
- `--approval-amount` binds an ERC-20 approval to an exact operation amount;
  operator-style ERC-1155 approval remains Boolean by contract design.
- An approval write requires all of `approve`, `--execute`, `--evidence`, and
  the exact owner/scope confirmation phrase printed by `probe`.
- A position write requires all of `position-execute`, `--execute`,
  `--evidence`, and the exact operation/owner/condition/amount/token-evidence
  confirmation phrase printed by `position-probe`.
- The signer address must equal `--owner`.
- By default the private key is read with console echo disabled. An operator
  may instead name one local, regular, non-symlink secret file explicitly with
  `--secret-env-file`; this exception exists only in this BNB-testnet harness.
  Raw key arguments, process-environment keys and API keys remain unsupported.
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

The probe validates remote chain id `97`, reads both the native BNB gas balance
and the registered testnet collateral balance, and checks every step in the
selected scope. The two assets are reported separately so funded gas can never
be mistaken for spendable Predict collateral. It sends no transaction and
prints the exact confirmation phrase required for a later write.

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

The program then prompts for the EOA key with terminal echo disabled. For an
operator-authorized unattended testnet acceptance run, append
`--secret-env-file .env.local`; the file must define
`PREDICTFUN_BNB_TESTNET_PRIVATE_KEY` and may define the matching
`PREDICTFUN_BNB_TESTNET_WALLET_ADDRESS`. The file path and key never enter
evidence or diagnostics. The tool
re-checks the selected approval, sends only missing steps, waits for receipts
at a 300 ms polling interval, and performs a postflight balance/approval read.
Already-satisfied scopes exit successfully without asking for a key or sending
a transaction.

For a single split, prefer an exact bounded allowance:

```sh
./build/dev/predictfun_testnet_acceptance.exe approve \
  --owner 0xYOUR_ADDRESS --scope split \
  --approval-amount 1000000000000000000 \
  --execute --confirm "EXACT PHRASE PRINTED BY PROBE" \
  --evidence runtime/predict-testnet-split-approval.jsonl
```

The amount becomes part of the confirmation phrase. Postflight reporting uses
that same bound rather than incorrectly requiring the default MaxInt256
threshold.

There is intentionally no mainnet flag and no raw private-key flag. The SDK
signer and production transports still do not discover secrets from files or
the process environment.

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
asks for the key through the hidden console prompt or reads the explicitly
named local secret file. In both cases the derived signer address must equal
`--owner`. It then submits exactly one transaction, waits for its receipt, and
records postflight balances. A lost or ambiguous response is never retried
automatically.

## Evidence schema

The append-only JSONL schema is `predictfun.testnet.acceptance.v1`. Events are
timestamped in Unix milliseconds and include:

- session mode, owner, scope and chain id;
- chain validation;
- before/after native BNB gas balance in wei and registered-collateral balance
  in exact base units;
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

## Completed live acceptance

On 2026-08-14 an explicitly authorized isolated BNB-testnet wallet completed:

- standard split, merge and winning-outcome redeem;
- negative-risk split;
- category conversion from market-798 NO to market-799 YES using the category
  `questionIndex` bitmask; and
- negative-risk redeem of the converted winning YES.

Every operation passed `eth_call`, produced one confirmed receipt, and matched
its expected before/after collateral and ERC-1155 balances. The conversion plus
redeem path restored the original collateral amount. Exact transaction hashes
are recorded in `DEVELOPMENT_PROGRESS.md`; the detailed JSONL evidence remains
under ignored `runtime/` and is not part of the source repository.
