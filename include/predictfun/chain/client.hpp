#pragma once

#include "predictfun/chain/operations.hpp"
#include "predictfun/chain/transaction.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/types/chain.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <functional>
#include <memory>
#include <string>

namespace predictfun::chain {

struct ClientOptions {
  ChainId expected_chain_id{ChainId::bnb_testnet};
  RpcEndpoint endpoint{default_rpc_endpoint(ChainId::bnb_testnet)};
  std::size_t max_response_bytes{2U * 1024U * 1024U};
};

template <class T> using Handler = std::function<void(Result<T>)>;

class ChainClient {
public:
  ChainClient(boost::asio::any_io_executor executor,
              std::shared_ptr<net::HttpTransport> transport,
              ClientOptions options = {});
  ~ChainClient();

  ChainClient(const ChainClient &) = delete;
  ChainClient &operator=(const ChainClient &) = delete;
  ChainClient(ChainClient &&) noexcept;
  ChainClient &operator=(ChainClient &&) noexcept;

  void async_chain_id(net::RequestContext context, Handler<ChainId> handler);
  void async_call(CallRequest request, BlockTag block,
                  net::RequestContext context, Handler<std::string> handler);
  void async_transaction_receipt(std::string transaction_hash,
                                 net::RequestContext context,
                                 Handler<std::optional<TransactionReceipt>> handler);
  void async_transaction_count(EvmAddress address, BlockTag block,
                               net::RequestContext context,
                               Handler<Uint256> handler);
  void async_gas_price(net::RequestContext context, Handler<Uint256> handler);
  void async_estimate_gas(EvmAddress from, UnsignedTransaction transaction,
                          net::RequestContext context, Handler<Uint256> handler);
  void async_populate_transaction(EvmAddress from,
                                  UnsignedTransaction transaction,
                                  net::RequestContext context,
                                  Handler<PopulatedTransaction> handler);
  void async_send_raw_transaction(RawTransaction transaction,
                                  net::RequestContext context,
                                  Handler<TransactionSubmission> handler);
  void async_wait_transaction_receipt(
      std::string transaction_hash, ReceiptWaitOptions options,
      net::RequestContext context, Handler<TransactionReceipt> handler);
  void async_execute_transaction(
      EvmAddress from, UnsignedTransaction transaction,
      TransactionDigestSigner signer, ReceiptWaitOptions options,
      net::RequestContext context, Handler<TransactionExecution> handler);

  void async_erc20_balance(EvmAddress token, EvmAddress owner,
                           net::RequestContext context, Handler<Uint256> handler);
  void async_erc20_allowance(EvmAddress token, EvmAddress owner,
                             EvmAddress spender, net::RequestContext context,
                             Handler<Uint256> handler);
  void async_erc1155_balance(EvmAddress token, EvmAddress owner,
                             Uint256 token_id, net::RequestContext context,
                             Handler<Uint256> handler);
  void async_erc1155_approved(EvmAddress token, EvmAddress owner,
                              EvmAddress operator_address,
                              net::RequestContext context,
                              Handler<bool> handler);

  void async_check_approvals(EvmAddress owner,
                             std::vector<ApprovalStep> steps,
                             net::RequestContext context,
                             Handler<std::vector<ApprovalCheck>> handler);
  void async_run_approvals(
      EvmAddress owner, EvmAddress transaction_sender,
      std::vector<ApprovalStep> steps, RouteOptions route,
      TransactionDigestSigner signer, ApprovalRunOptions options,
      std::function<void(const ApprovalProgress &)> progress,
      net::RequestContext context, Handler<ApprovalRunReport> handler);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::chain
