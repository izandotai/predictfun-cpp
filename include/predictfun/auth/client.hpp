#pragma once

#include "predictfun/codec/auth.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/types/market.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace predictfun::auth {

using ApiKeyProvider = std::function<std::string()>;
template <class T> using Handler = std::function<void(Result<T>)>;

struct ClientOptions {
  Environment environment{Environment::bnb_testnet};
  ApiKeyProvider api_key;
  codec::AuthCodecLimits codec_limits;
};

class MessageSigner {
public:
  virtual ~MessageSigner() = default;

  [[nodiscard]] virtual EvmAddress signer_address() const = 0;
  virtual void async_sign_message(std::string message,
                                  Handler<std::string> handler) = 0;
};

namespace protocol {

[[nodiscard]] constexpr std::string_view auth_message_target() noexcept {
  return "/v1/auth/message";
}

[[nodiscard]] constexpr std::string_view auth_exchange_target() noexcept {
  return "/v1/auth";
}

} // namespace protocol

class AuthClient {
public:
  AuthClient(boost::asio::any_io_executor executor,
             std::shared_ptr<net::HttpTransport> transport,
             ClientOptions options = {});
  ~AuthClient();

  AuthClient(const AuthClient &) = delete;
  AuthClient &operator=(const AuthClient &) = delete;
  AuthClient(AuthClient &&) noexcept;
  AuthClient &operator=(AuthClient &&) noexcept;

  void async_get_message(net::RequestContext context,
                         Handler<AuthMessage> handler);
  void async_exchange_token(AuthProof proof, net::RequestContext context,
                            Handler<WalletJwt> handler);
  void async_authenticate(std::shared_ptr<MessageSigner> signer,
                          net::RequestContext context,
                          Handler<WalletJwt> handler);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::auth
