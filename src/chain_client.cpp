#include "predictfun/chain/client.hpp"

#include "predictfun/chain/abi.hpp"

#include <glaze/glaze.hpp>

#include <atomic>
#include <cctype>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace predictfun::chain {
namespace {

constexpr auto read_options = glz::opts{.error_on_unknown_keys = false};

struct WireRpcError {
  std::int64_t code{0};
  std::string message;
};
struct WireStringResponse {
  std::string jsonrpc;
  std::uint64_t id{0U};
  std::optional<std::string> result;
  std::optional<WireRpcError> error;
};
struct WireReceipt {
  std::optional<std::string> transactionHash;
  std::optional<std::string> blockHash;
  std::optional<std::string> blockNumber;
  std::optional<std::string> status;
  std::optional<std::string> gasUsed;
  std::optional<std::string> effectiveGasPrice;
};
struct WireReceiptResponse {
  std::string jsonrpc;
  std::uint64_t id{0U};
  std::optional<WireReceipt> result;
  std::optional<WireRpcError> error;
};

bool transaction_hash(std::string_view value) {
  return value.size() == 66U && value.starts_with("0x") &&
         std::ranges::all_of(value.substr(2U), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

std::string block_tag(BlockTag tag) {
  switch (tag) {
  case BlockTag::latest: return "latest";
  case BlockTag::safe: return "safe";
  case BlockTag::finalized: return "finalized";
  case BlockTag::pending: return "pending";
  }
  return "latest";
}

Error rpc_error(const WireRpcError &value) {
  return Error{ErrorCode::remote_error,
               std::format("JSON-RPC error {}: {}", value.code,
                           value.message),
               {}};
}

template <class Wire>
Result<Wire> parse_response(const net::HttpResponse &response,
                            std::size_t limit, std::uint64_t id) {
  if (response.status != 200)
    return Error{response.status >= 500 ? ErrorCode::http_server_error
                                        : ErrorCode::http_client_error,
                 "BNB JSON-RPC returned a non-200 status", {},
                 response.status};
  if (response.body.size() > limit)
    return Error{ErrorCode::body_too_large,
                 "BNB JSON-RPC response exceeds configured limit", {}};
  Wire value;
  const auto parsed = glz::read<read_options>(value, response.body);
  if (parsed)
    return Error{ErrorCode::malformed_json,
                 "BNB JSON-RPC response contains malformed JSON", {}};
  if (value.jsonrpc != "2.0" || value.id != id)
    return Error{ErrorCode::protocol_error,
                 "BNB JSON-RPC response id/version mismatch", {}};
  if (value.error) return rpc_error(*value.error);
  return value;
}

} // namespace

struct ChainClient::Impl : public std::enable_shared_from_this<Impl> {
  using RawHandler = std::function<void(Result<net::HttpResponse>)>;

  Impl(boost::asio::any_io_executor executor_value,
       std::shared_ptr<net::HttpTransport> transport_value,
       ClientOptions options_value)
      : executor(std::move(executor_value)), transport(std::move(transport_value)),
        options(std::move(options_value)) {
    if (!transport) throw std::invalid_argument("BNB RPC transport is required");
    if (options.endpoint.host.empty() || options.endpoint.target.empty() ||
        !options.endpoint.target.starts_with('/'))
      throw std::invalid_argument("BNB RPC endpoint is invalid");
  }

  void request(std::string body, net::RequestContext context,
               RawHandler handler) {
    net::HttpRequest request;
    request.method = net::HttpMethod::post;
    request.host = options.endpoint.host;
    request.port = options.endpoint.port;
    request.target = options.endpoint.target;
    request.use_tls = options.endpoint.use_tls;
    request.content_type = "application/json";
    request.body = std::move(body);
    transport->async_post(std::move(request), std::move(context),
                          std::move(handler));
  }

  std::uint64_t next_id() noexcept { return request_id.fetch_add(1U); }

  void raw_chain_id(net::RequestContext context, Handler<ChainId> handler) {
    const auto id = next_id();
    request(std::format(
                R"({{"jsonrpc":"2.0","id":{},"method":"eth_chainId","params":[]}})",
                id),
            std::move(context),
            [self = shared_from_this(), id,
             handler = std::move(handler)](Result<net::HttpResponse> response) mutable {
              if (!response) return handler(response.error());
              auto wire = parse_response<WireStringResponse>(
                  response.value(), self->options.max_response_bytes, id);
              if (!wire) return handler(wire.error());
              if (!wire.value().result)
                return handler(Error{ErrorCode::missing_field,
                                     "eth_chainId result is missing", "result"});
              auto value = abi::decode_quantity(*wire.value().result);
              if (!value) return handler(value.error());
              ChainId chain;
              if (value.value().to_string() == "56")
                chain = ChainId::bnb_mainnet;
              else if (value.value().to_string() == "97")
                chain = ChainId::bnb_testnet;
              else
                return handler(Error{ErrorCode::unsupported_chain,
                                     "RPC is not a supported BNB chain",
                                     "chain_id"});
              handler(chain);
            });
  }

  void ensure_chain(net::RequestContext context,
                    std::function<void(Result<ChainId>)> handler) {
    std::optional<ChainId> cached;
    {
      std::scoped_lock lock(chain_mutex);
      cached = validated_chain;
    }
    if (cached) return handler(*cached);
    raw_chain_id(std::move(context),
                 [self = shared_from_this(), handler = std::move(handler)](
                     Result<ChainId> chain) mutable {
                   if (!chain) return handler(chain.error());
                   if (chain.value() != self->options.expected_chain_id)
                     return handler(Error{ErrorCode::unsupported_chain,
                                          "RPC chain id does not match the configured Predict environment",
                                          "chain_id"});
                   {
                     std::scoped_lock lock(self->chain_mutex);
                     self->validated_chain = chain.value();
                   }
                   handler(chain.value());
                 });
  }

  boost::asio::any_io_executor executor;
  std::shared_ptr<net::HttpTransport> transport;
  ClientOptions options;
  std::atomic<std::uint64_t> request_id{1U};
  std::mutex chain_mutex;
  std::optional<ChainId> validated_chain;
};

ChainClient::ChainClient(boost::asio::any_io_executor executor,
                         std::shared_ptr<net::HttpTransport> transport,
                         ClientOptions options)
    : impl_(std::make_shared<Impl>(std::move(executor), std::move(transport),
                                  std::move(options))) {}
ChainClient::~ChainClient() = default;
ChainClient::ChainClient(ChainClient &&) noexcept = default;
ChainClient &ChainClient::operator=(ChainClient &&) noexcept = default;

void ChainClient::async_chain_id(net::RequestContext context,
                                 Handler<ChainId> handler) {
  impl_->raw_chain_id(std::move(context), std::move(handler));
}

void ChainClient::async_call(CallRequest call, BlockTag block,
                             net::RequestContext context,
                             Handler<std::string> handler) {
  auto self = impl_;
  self->ensure_chain(
      context,
      [self, call = std::move(call), block, context,
       handler = std::move(handler)](Result<ChainId> chain) mutable {
        if (!chain) return handler(chain.error());
        const auto id = self->next_id();
        const auto from = call.from
                              ? std::format(R"(,"from":"{}")",
                                            call.from->to_string())
                              : std::string{};
        auto body = std::format(
            R"({{"jsonrpc":"2.0","id":{},"method":"eth_call","params":[{{"to":"{}","data":"{}"{}}},"{}"]}})",
            id, call.to.to_string(), call.data, from, block_tag(block));
        self->request(
            std::move(body), std::move(context),
            [self, id, handler = std::move(handler)](
                Result<net::HttpResponse> response) mutable {
              if (!response) return handler(response.error());
              auto wire = parse_response<WireStringResponse>(
                  response.value(), self->options.max_response_bytes, id);
              if (!wire) return handler(wire.error());
              if (!wire.value().result)
                return handler(Error{ErrorCode::missing_field,
                                     "eth_call result is missing", "result"});
              handler(std::move(*wire.value().result));
            });
      });
}

void ChainClient::async_transaction_receipt(
    std::string hash, net::RequestContext context,
    Handler<std::optional<TransactionReceipt>> handler) {
  if (!transaction_hash(hash))
    return handler(Error{ErrorCode::invalid_argument,
                         "transaction hash must be 0x plus 64 hex digits",
                         "transaction_hash"});
  auto self = impl_;
  self->ensure_chain(
      context,
      [self, hash = std::move(hash), context,
       handler = std::move(handler)](Result<ChainId> chain) mutable {
        if (!chain) return handler(chain.error());
        const auto id = self->next_id();
        self->request(
            std::format(
                R"({{"jsonrpc":"2.0","id":{},"method":"eth_getTransactionReceipt","params":["{}"]}})",
                id, hash),
            std::move(context),
            [self, id, handler = std::move(handler)](
                Result<net::HttpResponse> response) mutable {
              if (!response) return handler(response.error());
              auto wire = parse_response<WireReceiptResponse>(
                  response.value(), self->options.max_response_bytes, id);
              if (!wire) return handler(wire.error());
              if (!wire.value().result)
                return handler(std::optional<TransactionReceipt>{});
              const auto &value = *wire.value().result;
              if (!value.transactionHash)
                return handler(Error{ErrorCode::missing_field,
                                     "receipt transaction hash is missing",
                                     "result.transactionHash"});
              TransactionReceipt receipt;
              receipt.transaction_hash = *value.transactionHash;
              receipt.block_hash = value.blockHash;
              auto parse = [](const std::optional<std::string> &source,
                              std::optional<Uint256> &target) -> Result<bool> {
                if (!source) return true;
                auto decoded = abi::decode_quantity(*source);
                if (!decoded) return decoded.error();
                target = std::move(decoded.value());
                return true;
              };
              if (auto result = parse(value.blockNumber, receipt.block_number);
                  !result)
                return handler(result.error());
              if (auto result = parse(value.status, receipt.status); !result)
                return handler(result.error());
              if (auto result = parse(value.gasUsed, receipt.gas_used); !result)
                return handler(result.error());
              if (auto result = parse(value.effectiveGasPrice,
                                      receipt.effective_gas_price);
                  !result)
                return handler(result.error());
              handler(std::optional<TransactionReceipt>{std::move(receipt)});
            });
      });
}

void ChainClient::async_erc20_balance(EvmAddress token, EvmAddress owner,
                                      net::RequestContext context,
                                      Handler<Uint256> handler) {
  auto data = abi::erc20_balance_of(owner);
  if (!data) return handler(data.error());
  async_call(CallRequest{token, std::move(data.value()), std::nullopt},
             BlockTag::latest, std::move(context),
             [handler = std::move(handler)](Result<std::string> value) mutable {
               if (!value) return handler(value.error());
               handler(abi::decode_word_uint256(value.value()));
             });
}

void ChainClient::async_erc20_allowance(EvmAddress token, EvmAddress owner,
                                        EvmAddress spender,
                                        net::RequestContext context,
                                        Handler<Uint256> handler) {
  auto data = abi::erc20_allowance(owner, spender);
  if (!data) return handler(data.error());
  async_call(CallRequest{token, std::move(data.value()), std::nullopt},
             BlockTag::latest, std::move(context),
             [handler = std::move(handler)](Result<std::string> value) mutable {
               if (!value) return handler(value.error());
               handler(abi::decode_word_uint256(value.value()));
             });
}

void ChainClient::async_erc1155_balance(EvmAddress token, EvmAddress owner,
                                        Uint256 token_id,
                                        net::RequestContext context,
                                        Handler<Uint256> handler) {
  auto data = abi::erc1155_balance_of(owner, token_id);
  if (!data) return handler(data.error());
  async_call(CallRequest{token, std::move(data.value()), std::nullopt},
             BlockTag::latest, std::move(context),
             [handler = std::move(handler)](Result<std::string> value) mutable {
               if (!value) return handler(value.error());
               handler(abi::decode_word_uint256(value.value()));
             });
}

void ChainClient::async_erc1155_approved(
    EvmAddress token, EvmAddress owner, EvmAddress operator_address,
    net::RequestContext context, Handler<bool> handler) {
  auto data = abi::erc1155_is_approved_for_all(owner, operator_address);
  if (!data) return handler(data.error());
  async_call(CallRequest{token, std::move(data.value()), std::nullopt},
             BlockTag::latest, std::move(context),
             [handler = std::move(handler)](Result<std::string> value) mutable {
               if (!value) return handler(value.error());
               handler(abi::decode_word_bool(value.value()));
             });
}

} // namespace predictfun::chain
