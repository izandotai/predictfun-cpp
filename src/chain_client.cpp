#include "predictfun/chain/client.hpp"

#include "predictfun/chain/abi.hpp"
#include "predictfun/chain/approvals.hpp"
#include "predictfun/chain/transaction.hpp"

#include <boost/asio/steady_timer.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <glaze/glaze.hpp>

#include <atomic>
#include <cctype>
#include <format>
#include <memory>
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

bool same_hash(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::ranges::equal(left, right, [](unsigned char lhs,
                                           unsigned char rhs) {
           return std::tolower(lhs) == std::tolower(rhs);
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

Result<Uint256> with_gas_margin(const Uint256 &gas) {
  boost::multiprecision::cpp_int value{gas.to_string()};
  value = (value * 125 + 99) / 100;
  return Uint256::parse(value.convert_to<std::string>());
}

bool sufficient_exchange_allowance(const Uint256 &allowance) {
  static const boost::multiprecision::cpp_int minimum{
      "57896044618658097711785492504343953926634992332820282019728792003956564819967"};
  return boost::multiprecision::cpp_int{allowance.to_string()} >= minimum;
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

  void string_method(std::string method, std::string params,
                     net::RequestContext context, Handler<std::string> handler) {
    ensure_chain(
        context,
        [self = shared_from_this(), method = std::move(method),
         params = std::move(params), context,
         handler = std::move(handler)](Result<ChainId> chain) mutable {
          if (!chain) return handler(chain.error());
          const auto id = self->next_id();
          self->request(
              std::format(
                  R"({{"jsonrpc":"2.0","id":{},"method":"{}","params":{}}})",
                  id, method, params),
              std::move(context),
              [self, id, handler = std::move(handler)](
                  Result<net::HttpResponse> response) mutable {
                if (!response) return handler(response.error());
                auto wire = parse_response<WireStringResponse>(
                    response.value(), self->options.max_response_bytes, id);
                if (!wire) return handler(wire.error());
                if (!wire.value().result)
                  return handler(Error{ErrorCode::missing_field,
                                       "JSON-RPC string result is missing",
                                       "result"});
                handler(std::move(*wire.value().result));
              });
        });
  }

  void call(CallRequest call_request, BlockTag block,
            net::RequestContext context, Handler<std::string> handler) {
    ensure_chain(
        context,
        [self = shared_from_this(), call_request = std::move(call_request),
         block, context,
         handler = std::move(handler)](Result<ChainId> chain) mutable {
          if (!chain) return handler(chain.error());
          const auto id = self->next_id();
          const auto from =
              call_request.from
                  ? std::format(R"(,"from":"{}")",
                                call_request.from->to_string())
                  : std::string{};
          auto body = std::format(
              R"({{"jsonrpc":"2.0","id":{},"method":"eth_call","params":[{{"to":"{}","data":"{}"{}}},"{}"]}})",
              id, call_request.to.to_string(), call_request.data, from,
              block_tag(block));
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

  void receipt(std::string hash, net::RequestContext context,
               Handler<std::optional<TransactionReceipt>> handler) {
    if (!transaction_hash(hash))
      return handler(Error{ErrorCode::invalid_argument,
                           "transaction hash must be 0x plus 64 hex digits",
                           "transaction_hash"});
    ensure_chain(
        context,
        [self = shared_from_this(), hash = std::move(hash), context,
         handler = std::move(handler)](Result<ChainId> chain) mutable {
          if (!chain) return handler(chain.error());
          const auto id = self->next_id();
          self->request(
              std::format(
                  R"({{"jsonrpc":"2.0","id":{},"method":"eth_getTransactionReceipt","params":["{}"]}})",
                  id, hash),
              std::move(context),
              [self, id, expected = hash, handler = std::move(handler)](
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
                if (!transaction_hash(*value.transactionHash) ||
                    !same_hash(*value.transactionHash, expected))
                  return handler(Error{ErrorCode::protocol_error,
                                       "receipt transaction hash mismatch",
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
                if (auto result = parse(value.gasUsed, receipt.gas_used);
                    !result)
                  return handler(result.error());
                if (auto result = parse(value.effectiveGasPrice,
                                        receipt.effective_gas_price);
                    !result)
                  return handler(result.error());
                handler(std::optional<TransactionReceipt>{std::move(receipt)});
              });
        });
  }

  void populate(EvmAddress from, UnsignedTransaction transaction,
                net::RequestContext context,
                Handler<PopulatedTransaction> handler) {
    string_method(
        "eth_getTransactionCount",
        std::format(R"(["{}","pending"])", from.to_string()), context,
        [self = shared_from_this(), from,
         transaction = std::move(transaction), context,
         handler = std::move(handler)](Result<std::string> nonce_wire) mutable {
          if (!nonce_wire) return handler(nonce_wire.error());
          auto nonce = abi::decode_quantity(nonce_wire.value());
          if (!nonce) return handler(nonce.error());
          self->string_method(
              "eth_gasPrice", "[]", context,
              [self, from, transaction = std::move(transaction), context,
               nonce = std::move(nonce.value()),
               handler = std::move(handler)](
                  Result<std::string> gas_price_wire) mutable {
                if (!gas_price_wire) return handler(gas_price_wire.error());
                auto gas_price = abi::decode_quantity(gas_price_wire.value());
                if (!gas_price) return handler(gas_price.error());
                auto value = abi::encode_quantity(transaction.value);
                if (!value) return handler(value.error());
                self->string_method(
                    "eth_estimateGas",
                    std::format(
                        R"([{{"from":"{}","to":"{}","data":"{}","value":"{}"}}])",
                        from.to_string(), transaction.to.to_string(),
                        transaction.data, value.value()),
                    context,
                    [self, from, transaction = std::move(transaction),
                     nonce = std::move(nonce),
                     gas_price = std::move(gas_price.value()),
                     handler = std::move(handler)](
                        Result<std::string> gas_wire) mutable {
                      if (!gas_wire) return handler(gas_wire.error());
                      auto gas = abi::decode_quantity(gas_wire.value());
                      if (!gas) return handler(gas.error());
                      auto limit = with_gas_margin(gas.value());
                      if (!limit) return handler(limit.error());
                      handler(PopulatedTransaction{
                          self->options.expected_chain_id, from,
                          transaction.to, std::move(nonce),
                          std::move(gas_price), std::move(limit.value()),
                          transaction.value, std::move(transaction.data)});
                    });
              });
        });
  }

  void send_raw(RawTransaction transaction, net::RequestContext context,
                Handler<TransactionSubmission> handler) {
    auto calculated = raw_transaction_hash(transaction.bytes);
    if (!calculated) return handler(calculated.error());
    if (!transaction_hash(transaction.transaction_hash) ||
        !same_hash(transaction.transaction_hash, calculated.value()))
      return handler(Error{ErrorCode::invalid_argument,
                           "raw transaction hash does not match its bytes",
                           "transaction_hash"});
    ensure_chain(
        context,
        [self = shared_from_this(), transaction = std::move(transaction),
         context,
         handler = std::move(handler)](Result<ChainId> chain) mutable {
          if (!chain) return handler(chain.error());
          const auto id = self->next_id();
          self->request(
              std::format(
                  R"({{"jsonrpc":"2.0","id":{},"method":"eth_sendRawTransaction","params":["{}"]}})",
                  id, transaction.bytes),
              std::move(context),
              [self, id, expected = transaction.transaction_hash,
               handler = std::move(handler)](
                  Result<net::HttpResponse> response) mutable {
                if (!response)
                  return handler(TransactionSubmission{
                      TransactionSubmissionState::ambiguous, expected,
                      response.error()});
                auto wire = parse_response<WireStringResponse>(
                    response.value(), self->options.max_response_bytes, id);
                if (!wire) return handler(wire.error());
                if (!wire.value().result ||
                    !transaction_hash(*wire.value().result) ||
                    !same_hash(*wire.value().result, expected))
                  return handler(Error{
                      ErrorCode::protocol_error,
                      "eth_sendRawTransaction returned an unexpected hash",
                      "result"});
                handler(TransactionSubmission{
                    TransactionSubmissionState::accepted, expected,
                    std::nullopt});
              });
        });
  }

  void wait_receipt(std::string hash, ReceiptWaitOptions wait_options,
                    net::RequestContext context,
                    Handler<TransactionReceipt> handler) {
    if (wait_options.poll_interval <= std::chrono::milliseconds::zero())
      return handler(Error{ErrorCode::invalid_argument,
                           "receipt poll interval must be positive",
                           "poll_interval"});
    if (context.deadline == std::chrono::steady_clock::time_point{})
      context.deadline =
          std::chrono::steady_clock::now() + std::chrono::minutes{2};
    struct State : public std::enable_shared_from_this<State> {
      std::shared_ptr<Impl> client;
      std::string hash;
      ReceiptWaitOptions options;
      net::RequestContext context;
      Handler<TransactionReceipt> handler;
      boost::asio::steady_timer timer;
      bool done{false};

      State(std::shared_ptr<Impl> value, std::string transaction_hash_value,
            ReceiptWaitOptions options_value,
            net::RequestContext context_value,
            Handler<TransactionReceipt> completion)
          : client(std::move(value)),
            hash(std::move(transaction_hash_value)), options(options_value),
            context(std::move(context_value)), handler(std::move(completion)),
            timer(client->executor) {}

      void finish(Result<TransactionReceipt> result) {
        if (done) return;
        done = true;
        handler(std::move(result));
      }

      void poll() {
        if (context.cancel.stop_requested())
          return finish(
              Error{ErrorCode::cancelled, "receipt wait cancelled", {}});
        if (std::chrono::steady_clock::now() >= context.deadline)
          return finish(Error{ErrorCode::deadline_exceeded,
                              "receipt wait deadline exceeded", {}});
        client->receipt(
            hash, context,
            [self = shared_from_this()](
                Result<std::optional<TransactionReceipt>> result) mutable {
              if (!result) return self->finish(result.error());
              if (result.value()) {
                if (!result.value()->status)
                  return self->finish(Error{
                      ErrorCode::missing_field,
                      "confirmed receipt status is missing", "receipt.status"});
                if (result.value()->confirmed_revert())
                  return self->finish(Error{ErrorCode::execution_reverted,
                                            "transaction execution reverted",
                                            "receipt.status"});
                return self->finish(std::move(*result.value()));
              }
              const auto wake = std::chrono::steady_clock::now() +
                                self->options.poll_interval;
              if (wake >= self->context.deadline)
                return self->finish(Error{ErrorCode::deadline_exceeded,
                                          "receipt wait deadline exceeded",
                                          {}});
              self->timer.expires_at(wake);
              self->timer.async_wait(
                  [self](const boost::system::error_code &error) {
                    if (error)
                      return self->finish(Error{
                          ErrorCode::cancelled,
                          "receipt wait timer cancelled: " + error.message(),
                          {}});
                    self->poll();
                  });
            });
      }
    };
    auto state = std::make_shared<State>(
        shared_from_this(), std::move(hash), wait_options, std::move(context),
        std::move(handler));
    state->poll();
  }

  void execute(EvmAddress from, UnsignedTransaction transaction,
               TransactionDigestSigner signer,
               ReceiptWaitOptions wait_options, net::RequestContext context,
               Handler<TransactionExecution> handler) {
    if (!signer)
      return handler(Error{ErrorCode::invalid_argument,
                           "transaction digest signer is required", "signer"});
    populate(
        from, std::move(transaction), context,
        [self = shared_from_this(), signer = std::move(signer), wait_options,
         context,
         handler = std::move(handler)](
            Result<PopulatedTransaction> populated) mutable {
          if (!populated) return handler(populated.error());
          Result<RawTransaction> raw = Error{
              ErrorCode::invalid_argument, "transaction signer failed", "signer"};
          try {
            raw = sign_legacy_transaction(populated.value(), signer);
          } catch (...) {
            return handler(Error{ErrorCode::invalid_argument,
                                 "transaction signer threw an exception",
                                 "signer"});
          }
          if (!raw) return handler(raw.error());
          auto populated_value = std::move(populated.value());
          auto raw_value = std::move(raw.value());
          // Keep the archival copy distinct from the value handed to the
          // transport. Function-argument evaluation order must not be able to
          // move the bytes out before send_raw validates them.
          auto raw_for_submission = raw_value;
          self->send_raw(
              std::move(raw_for_submission), context,
              [self, populated_value = std::move(populated_value),
               raw_value = std::move(raw_value), wait_options, context,
               handler = std::move(handler)](
                  Result<TransactionSubmission> submitted) mutable {
                if (!submitted) return handler(submitted.error());
                auto submission = std::move(submitted.value());
                auto transaction_hash_value = raw_value.transaction_hash;
                self->wait_receipt(
                    std::move(transaction_hash_value), wait_options, context,
                    [populated_value = std::move(populated_value),
                     raw_value = std::move(raw_value),
                     submission = std::move(submission),
                     handler = std::move(handler)](
                        Result<TransactionReceipt> receipt) mutable {
                      if (!receipt) {
                        const auto state =
                            receipt.error().code == ErrorCode::execution_reverted
                                ? TransactionExecutionState::reverted
                                : TransactionExecutionState::outcome_unknown;
                        return handler(TransactionExecution{
                            state, std::move(populated_value),
                            std::move(raw_value), std::move(submission),
                            std::nullopt, receipt.error()});
                      }
                      auto receipt_value = std::move(receipt.value());
                      handler(TransactionExecution{
                          TransactionExecutionState::confirmed,
                          std::move(populated_value), std::move(raw_value),
                          std::move(submission), std::move(receipt_value),
                          std::nullopt});
                    });
              });
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
  impl_->call(std::move(call), block, std::move(context), std::move(handler));
}

void ChainClient::async_transaction_receipt(
    std::string hash, net::RequestContext context,
    Handler<std::optional<TransactionReceipt>> handler) {
  impl_->receipt(std::move(hash), std::move(context), std::move(handler));
}

void ChainClient::async_transaction_count(EvmAddress address, BlockTag block,
                                          net::RequestContext context,
                                          Handler<Uint256> handler) {
  impl_->string_method(
      "eth_getTransactionCount",
      std::format(R"(["{}","{}"])", address.to_string(), block_tag(block)),
      std::move(context),
      [handler = std::move(handler)](Result<std::string> value) mutable {
        if (!value) return handler(value.error());
        handler(abi::decode_quantity(value.value()));
      });
}

void ChainClient::async_gas_price(net::RequestContext context,
                                  Handler<Uint256> handler) {
  impl_->string_method(
      "eth_gasPrice", "[]", std::move(context),
      [handler = std::move(handler)](Result<std::string> value) mutable {
        if (!value) return handler(value.error());
        handler(abi::decode_quantity(value.value()));
      });
}

void ChainClient::async_estimate_gas(EvmAddress from,
                                     UnsignedTransaction transaction,
                                     net::RequestContext context,
                                     Handler<Uint256> handler) {
  auto value = abi::encode_quantity(transaction.value);
  if (!value) return handler(value.error());
  impl_->string_method(
      "eth_estimateGas",
      std::format(
          R"([{{"from":"{}","to":"{}","data":"{}","value":"{}"}}])",
          from.to_string(), transaction.to.to_string(), transaction.data,
          value.value()),
      std::move(context),
      [handler = std::move(handler)](Result<std::string> result) mutable {
        if (!result) return handler(result.error());
        handler(abi::decode_quantity(result.value()));
      });
}

void ChainClient::async_populate_transaction(
    EvmAddress from, UnsignedTransaction transaction,
    net::RequestContext context, Handler<PopulatedTransaction> handler) {
  impl_->populate(from, std::move(transaction), std::move(context),
                  std::move(handler));
}

void ChainClient::async_send_raw_transaction(
    RawTransaction transaction, net::RequestContext context,
    Handler<TransactionSubmission> handler) {
  impl_->send_raw(std::move(transaction), std::move(context),
                  std::move(handler));
}

void ChainClient::async_wait_transaction_receipt(
    std::string hash, ReceiptWaitOptions options,
    net::RequestContext context, Handler<TransactionReceipt> handler) {
  impl_->wait_receipt(std::move(hash), options, std::move(context),
                      std::move(handler));
}

void ChainClient::async_execute_transaction(
    EvmAddress from, UnsignedTransaction transaction,
    TransactionDigestSigner signer, ReceiptWaitOptions options,
    net::RequestContext context, Handler<TransactionExecution> handler) {
  impl_->execute(from, std::move(transaction), std::move(signer), options,
                 std::move(context), std::move(handler));
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

void ChainClient::async_erc20_decimals(EvmAddress token,
                                       net::RequestContext context,
                                       Handler<std::uint8_t> handler) {
  auto data = abi::erc20_decimals();
  if (!data) return handler(data.error());
  async_call(CallRequest{token, std::move(data.value()), std::nullopt},
             BlockTag::latest, std::move(context),
             [handler = std::move(handler)](Result<std::string> value) mutable {
               if (!value) return handler(value.error());
               auto decoded = abi::decode_word_uint256(value.value());
               if (!decoded) return handler(decoded.error());
               const auto &text = decoded.value().to_string();
               if (text.size() > 3U)
                 return handler(Error{ErrorCode::numeric_overflow,
                                      "ERC-20 decimals exceeds uint8", text});
               const auto number = std::stoul(text);
               if (number > 255U)
                 return handler(Error{ErrorCode::numeric_overflow,
                                      "ERC-20 decimals exceeds uint8", text});
               handler(static_cast<std::uint8_t>(number));
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

void ChainClient::async_check_approvals(
    EvmAddress owner, std::vector<ApprovalStep> steps,
    net::RequestContext context,
    Handler<std::vector<ApprovalCheck>> handler) {
  if (steps.empty()) return handler(std::vector<ApprovalCheck>{});

  struct State {
    std::mutex mutex;
    std::vector<std::optional<ApprovalCheck>> checks;
    std::size_t remaining{0U};
    std::optional<Error> first_error;
    Handler<std::vector<ApprovalCheck>> handler;
    bool done{false};

    void complete(std::size_t index, Result<ApprovalCheck> result) {
      Handler<std::vector<ApprovalCheck>> completion;
      Result<std::vector<ApprovalCheck>> final =
          Error{ErrorCode::protocol_error,
                "approval check did not complete", {}};
      {
        std::scoped_lock lock(mutex);
        if (done) return;
        if (result)
          checks[index] = std::move(result.value());
        else if (!first_error)
          first_error = result.error();
        if (--remaining != 0U) return;
        done = true;
        completion = std::move(handler);
        if (first_error) {
          final = *first_error;
        } else {
          std::vector<ApprovalCheck> ordered;
          ordered.reserve(checks.size());
          for (auto &check : checks)
            ordered.push_back(std::move(*check));
          final = std::move(ordered);
        }
      }
      completion(std::move(final));
    }
  };

  auto state = std::make_shared<State>();
  state->checks.resize(steps.size());
  state->remaining = steps.size();
  state->handler = std::move(handler);

  for (std::size_t index = 0U; index < steps.size(); ++index) {
    auto step = steps[index];
    Result<std::string> data =
        step.kind == ApprovalKind::erc20_allowance
            ? abi::erc20_allowance(owner, step.spender)
            : abi::erc1155_is_approved_for_all(owner, step.spender);
    if (!data) {
      state->complete(index, data.error());
      continue;
    }
    impl_->call(
        CallRequest{step.token, std::move(data.value()), std::nullopt},
        BlockTag::latest, context,
        [state, index, step = std::move(step)](
            Result<std::string> wire) mutable {
          if (!wire) return state->complete(index, wire.error());
          if (step.kind == ApprovalKind::erc20_allowance) {
            auto allowance = abi::decode_word_uint256(wire.value());
            if (!allowance)
              return state->complete(index, allowance.error());
            const bool satisfied =
                sufficient_exchange_allowance(allowance.value());
            return state->complete(
                index, ApprovalCheck{std::move(step), satisfied,
                                     std::move(allowance.value())});
          }
          auto approved = abi::decode_word_bool(wire.value());
          if (!approved) return state->complete(index, approved.error());
          state->complete(index, ApprovalCheck{std::move(step),
                                               approved.value(), std::nullopt});
        });
  }
}

void ChainClient::async_run_approvals(
    EvmAddress owner, EvmAddress transaction_sender,
    std::vector<ApprovalStep> steps, RouteOptions route,
    TransactionDigestSigner signer, ApprovalRunOptions options,
    std::function<void(const ApprovalProgress &)> progress,
    net::RequestContext context, Handler<ApprovalRunReport> handler) {
  if (!signer)
    return handler(Error{ErrorCode::invalid_argument,
                         "transaction digest signer is required", "signer"});
  std::vector<ApprovalStep> unique;
  unique.reserve(steps.size());
  for (auto &candidate : steps) {
    if (std::ranges::none_of(unique, [&](const ApprovalStep &existing) {
          return existing.id == candidate.id;
        }))
      unique.push_back(std::move(candidate));
  }

  struct State : public std::enable_shared_from_this<State> {
    std::shared_ptr<Impl> client;
    EvmAddress transaction_sender;
    std::vector<ApprovalStep> steps;
    RouteOptions route;
    TransactionDigestSigner signer;
    ApprovalRunOptions options;
    std::function<void(const ApprovalProgress &)> progress;
    net::RequestContext context;
    Handler<ApprovalRunReport> handler;
    ApprovalRunReport report;
    std::size_t index{0U};
    bool done{false};

    void notify(const ApprovalStep &step, ApprovalProgressState state,
                std::optional<std::string> hash = std::nullopt,
                std::optional<Error> issue = std::nullopt) const {
      if (!progress) return;
      try {
        progress(ApprovalProgress{step, state, std::move(hash),
                                  std::move(issue)});
      } catch (...) {
        // Progress reporting is observational and must not alter execution.
      }
    }

    void finish() {
      if (done) return;
      done = true;
      report.success =
          std::ranges::all_of(report.steps, [](const ApprovalStepResult &step) {
            return step.state == ApprovalProgressState::skipped ||
                   step.state == ApprovalProgressState::confirmed;
          });
      handler(std::move(report));
    }

    void fail_current(const ApprovalStep &step, Error error) {
      notify(step, ApprovalProgressState::failed, std::nullopt, error);
      report.steps.push_back(ApprovalStepResult{
          step, ApprovalProgressState::failed, std::nullopt, error});
      ++index;
      if (options.stop_on_error)
        finish();
      else
        next();
    }

    void submit(const ApprovalStep &step) {
      auto direct = approval_transaction(step);
      if (!direct) return fail_current(step, direct.error());
      auto transaction = route_transaction(
          client->options.expected_chain_id, std::move(direct.value()), route);
      if (!transaction) return fail_current(step, transaction.error());
      notify(step, ApprovalProgressState::submitting);
      client->execute(
          transaction_sender, std::move(transaction.value()), signer,
          options.receipt_wait, context,
          [self = shared_from_this(), step](
              Result<TransactionExecution> executed) mutable {
            if (!executed) return self->fail_current(step, executed.error());
            auto transaction_value = std::move(executed.value());
            if (!transaction_value.confirmed_success()) {
              auto issue = transaction_value.issue.value_or(Error{
                  ErrorCode::ambiguous_submission,
                  "approval transaction outcome is not confirmed", {}});
              self->notify(step, ApprovalProgressState::failed,
                           transaction_value.raw_transaction.transaction_hash,
                           issue);
              self->report.steps.push_back(ApprovalStepResult{
                  step, ApprovalProgressState::failed,
                  std::move(transaction_value), issue});
              ++self->index;
              if (self->options.stop_on_error)
                self->finish();
              else
                self->next();
              return;
            }
            self->notify(step, ApprovalProgressState::confirmed,
                         transaction_value.raw_transaction.transaction_hash);
            self->report.steps.push_back(ApprovalStepResult{
                step, ApprovalProgressState::confirmed,
                std::move(transaction_value), std::nullopt});
            ++self->index;
            self->next();
          });
    }

    void check_or_submit(const ApprovalStep &step) {
      notify(step, ApprovalProgressState::checking);
      Result<std::string> data =
          step.kind == ApprovalKind::erc20_allowance
              ? abi::erc20_allowance(owner, step.spender)
              : abi::erc1155_is_approved_for_all(owner, step.spender);
      if (!data) return submit(step);
      client->call(
          CallRequest{step.token, std::move(data.value()), std::nullopt},
          BlockTag::latest, context,
          [self = shared_from_this(), step](Result<std::string> wire) mutable {
            // Match the official SDK: a pre-check read failure falls through
            // to the send path rather than incorrectly declaring success.
            if (!wire) return self->submit(step);
            bool satisfied = false;
            if (step.kind == ApprovalKind::erc20_allowance) {
              auto allowance = abi::decode_word_uint256(wire.value());
              if (!allowance) return self->submit(step);
              satisfied = sufficient_exchange_allowance(allowance.value());
            } else {
              auto approved = abi::decode_word_bool(wire.value());
              if (!approved) return self->submit(step);
              satisfied = approved.value();
            }
            if (!satisfied) return self->submit(step);
            self->notify(step, ApprovalProgressState::skipped);
            self->report.steps.push_back(ApprovalStepResult{
                step, ApprovalProgressState::skipped, std::nullopt,
                std::nullopt});
            ++self->index;
            self->next();
          });
    }

    EvmAddress owner;

    void next() {
      if (done) return;
      if (context.cancel.stop_requested()) {
        if (index < steps.size())
          return fail_current(
              steps[index],
              Error{ErrorCode::cancelled, "approval run cancelled", {}});
        return finish();
      }
      if (index >= steps.size()) return finish();
      if (options.skip_satisfied)
        check_or_submit(steps[index]);
      else
        submit(steps[index]);
    }
  };

  auto state = std::make_shared<State>();
  state->client = impl_;
  state->owner = owner;
  state->transaction_sender = transaction_sender;
  state->steps = std::move(unique);
  state->route = route;
  state->signer = std::move(signer);
  state->options = options;
  state->progress = std::move(progress);
  state->context = std::move(context);
  state->handler = std::move(handler);
  state->next();
}

} // namespace predictfun::chain
