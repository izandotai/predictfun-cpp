#pragma once

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

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::chain
