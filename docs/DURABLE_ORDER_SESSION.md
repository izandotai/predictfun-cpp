# Durable order session

`predictfun::execution::DurableOrderSession` is the recommended composition
boundary for a host that submits Predict.fun orders. It combines the existing
single-attempt trading client, checksummed lifecycle journal and authenticated
REST reconciliation without acquiring signing, wallet, key-loading or RPC
authority.

## Guarantees

For every valid create request, the session performs this order:

1. validate the signed request and derive its exact 18-decimal share amount;
2. synchronously append the deterministic order hash as
   `submission_pending` to the lifecycle journal;
3. dispatch `POST /v1/orders` exactly once;
4. persist acknowledgement, explicit rejection, or an ambiguous outcome;
5. never infer failure from a timeout and never automatically replay create.

An invalid request reaches neither the journal nor the network. A known
rejection is terminal. An ambiguous transport/server result remains
nonterminal and requires reconciliation by deterministic hash/order snapshot.

## Opening a session

The caller constructs authenticated `TradingClient` and `PrivateRestClient`
instances and owns their JWT/API-key lifetime. The journal path should be
stable for the account and environment rather than unique per process run.

```cpp
auto opened = predictfun::execution::DurableOrderSession::open(
    io.get_executor(), trading_client, private_rest_client,
    "runtime/predict-orders.journal");
if (!opened) {
  // Fail closed; do not submit outside the session.
}
auto session = std::move(opened.value());
```

`async_submit_order` accepts an already built and signed
`CreateOrderRequest`. Its result distinguishes three cases:

- `acknowledged()`: the venue returned an order receipt;
- `rejected()`: a definite validation/client/venue rejection was journaled;
- `ambiguous()`: submission may have reached the venue and must not be resent.

## Private stream and reconciliation

Forward every decoded private wallet event to `async_apply_wallet_event`.
Book removal is not treated as an on-chain cancellation; forward it through
`async_mark_book_removed` and wait for an authoritative wallet/REST state.

The official private WebSocket supplies no initial account snapshot. After
each new stream generation or reconnect, call `async_reconcile` before allowing
new host decisions that depend on synchronized order state:

```cpp
session.async_reconcile(generation, context,
    [](predictfun::Result<predictfun::execution::ReconciliationResult> result) {
      if (!result) {
        // Keep mutation policy quarantined and retry the safe REST read later.
        return;
      }
      // All bounded order pages were consumed before this callback.
    });
```

The session rejects overlapping reconciliation runs, detects repeated cursors,
and bounds both page size and maximum pages. Safe authenticated GET retries may
occur inside `PrivateRestClient`; create-order mutation is never retried.

## Recovery rules

Opening an existing journal restores all records. Every recovered nonterminal
record stays reconciliation-required until an authoritative complete REST
snapshot observes it. A torn final journal record is repaired; corruption in a
completed record fails closed. The journal stores no JWT, API key, signature,
request body, private key or raw wallet event.

The application should use one journal per Predict account, network and
strategy authority domain, and must prevent two writers from opening the same
path concurrently.
