#pragma once

#include "predictfun/types/error.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace predictfun::net {

struct Header {
  std::string name;
  std::string value;
};

struct HttpRequest {
  std::string host;
  std::string port{"443"};
  std::string target{"/"};
  bool use_tls{true};
  std::vector<Header> headers;
};

struct HttpResponse {
  int status{0};
  std::string body;
  std::vector<Header> headers;

  [[nodiscard]] std::string_view header(std::string_view name) const noexcept;
};

struct TransportLimits {
  std::size_t max_body_bytes{2U * 1024U * 1024U};
  std::chrono::milliseconds default_timeout{15'000};
};

struct RequestContext {
  std::chrono::steady_clock::time_point deadline{};
  std::stop_token cancel;
  std::string trace_id;

  [[nodiscard]] static RequestContext
  with_timeout(std::chrono::milliseconds timeout, std::stop_token cancel = {});
};

using ResponseHandler = std::function<void(Result<HttpResponse>)>;

class HttpTransport {
public:
  virtual ~HttpTransport() = default;
  virtual void async_get(HttpRequest request, RequestContext context,
                         ResponseHandler handler) = 0;
};

class BeastHttpTransport final : public HttpTransport {
public:
  explicit BeastHttpTransport(boost::asio::any_io_executor executor,
                              TransportLimits limits = {});
  ~BeastHttpTransport() override;

  BeastHttpTransport(const BeastHttpTransport &) = delete;
  BeastHttpTransport &operator=(const BeastHttpTransport &) = delete;

  void async_get(HttpRequest request, RequestContext context,
                 ResponseHandler handler) override;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

[[nodiscard]] bool is_secret_header(std::string_view name) noexcept;
[[nodiscard]] std::string sanitized_request_summary(const HttpRequest &request);

} // namespace predictfun::net
