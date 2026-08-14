# Official Predict API coverage

Audited: 2026-08-14

This inventory compares `predictfun-cpp` with the public Predict developer
documentation and the official TypeScript/Python SDKs. It is the release
checklist for the supported general-purpose API surface; it is not an excuse
to invent endpoints for schema-only objects.

## REST

| Area | Official operation | C++ support |
|---|---|---|
| Authentication | `GET /v1/auth/message`, `POST /v1/auth` | typed, bounded |
| Discovery | `GET /v1/categories`, `GET /v1/categories/{slug}` | typed, bounded |
| Discovery | `GET /v1/tags`, `GET /v1/search` | typed, bounded |
| Markets | `GET /v1/markets`, `GET /v1/markets/{id}` | typed, bounded |
| Markets | order book, time series/latest, statistics and last sale | typed, bounded |
| Public trading data | `GET /v1/orders/matches` | typed, bounded |
| Public positions | `GET /v1/positions/{address}` | typed, bounded |
| Account | account, activity, positions and orders reads | typed, bounded |
| Account | authenticated referral assignment | single-attempt mutation with ambiguity key |
| Trading | create order, remove by ids | single-attempt mutations |
| Trading | `POST /orders/remove-by-hash` (maximum 100 hashes) | single-attempt mutation |
| Reconciliation | order lookup by deterministic hash, matches | typed, bounded |

OAuth is intentionally excluded because the official documentation identifies
it as a non-general-use integration flow. `BulletinBoard`, `FeeEvent`,
`PendingYield` and `OracleUpdate` remain schema-only names: no public route is
documented for them, so this SDK does not fabricate one.

## WebSocket

| Topic | C++ support |
|---|---|
| `predictOrderbook/{marketId}` | subscribe/unsubscribe and bounded decode |
| `predictTradingStatus/{marketId}` | subscribe/unsubscribe and bounded decode |
| `predictMarketStatus/{marketId}` | subscribe/unsubscribe and bounded decode |
| `predictMarketChanged/{marketId}` | subscribe/unsubscribe and bounded decode |
| `predictCategoryChanged/{categoryId}` | subscribe/unsubscribe and bounded decode |
| `predictWalletEvents/{jwt}` | private subscribe, bounded decode and reconnect reconciliation |

## Current response-model coverage

The public model preserves the currently documented lifecycle and metadata:

- market states `REGISTERED`, `PRICE_PROPOSED`, `PRICE_DISPUTED`, `PAUSED`,
  `UNPAUSED`, `RESOLVED` and `REMOVED`;
- order states `OPEN`, `FILLED`, `EXPIRED`, `CANCELLED` and `INVALIDATED`;
- market visibility, resolution/oracle identifiers, thresholds, boost data,
  external venue identifiers, market type, rewards and embedded statistics;
- category resolution provider, parent slug, statistics and team payloads;
- optional three-cent ask liquidity in market statistics.

Exact prices, quantities, identifiers and amounts stay in lossless integer or
fixed-decimal types. Irregular sports/team/variant payloads are retained as
bounded raw JSON strings instead of exposing the JSON library or silently
discarding future fields. Reward arrays, external identifiers and raw payloads
all have explicit decode limits.

## Verification rule

A coverage claim is accepted only after both Debug and Release test matrices
pass and an installed-package consumer links the public/read-only targets and
the explicitly opted-in signer target. Live funded mutation evidence remains a
separate caller-authorized acceptance gate.
