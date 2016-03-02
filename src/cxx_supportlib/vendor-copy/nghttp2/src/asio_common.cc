/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2015 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "asio_common.h"

#include <memory>

#include "util.h"
#include "template.h"
#include "http2.h"

namespace nghttp2 {
namespace asio_http2 {

class nghttp2_category_impl : public boost::system::error_category {
public:
  const char *name() const noexcept { return "nghttp2"; }
  std::string message(int ev) const { return nghttp2_strerror(ev); }
};

const boost::system::error_category &nghttp2_category() noexcept {
  static nghttp2_category_impl cat;
  return cat;
}

boost::system::error_code make_error_code(nghttp2_error ev) {
  return boost::system::error_code(static_cast<int>(ev), nghttp2_category());
}

class nghttp2_asio_category_impl : public boost::system::error_category {
public:
  const char *name() const noexcept { return "nghttp2_asio"; }
  std::string message(int ev) const {
    switch (ev) {
    case NGHTTP2_ASIO_ERR_NO_ERROR:
      return "no error";
    case NGHTTP2_ASIO_ERR_TLS_NO_APP_PROTO_NEGOTIATED:
      return "tls: no application protocol negotiated";
    default:
      return "unknown";
    }
  }
};

const boost::system::error_category &nghttp2_asio_category() noexcept {
  static nghttp2_asio_category_impl cat;
  return cat;
}

boost::system::error_code make_error_code(nghttp2_asio_error ev) {
  return boost::system::error_code(static_cast<int>(ev),
                                   nghttp2_asio_category());
}

generator_cb string_generator(std::string data) {
  auto strio = std::make_shared<std::pair<std::string, size_t>>(std::move(data),
                                                                data.size());
  return [strio](uint8_t *buf, size_t len, uint32_t *data_flags) {
    auto &data = strio->first;
    auto &left = strio->second;
    auto n = std::min(len, left);
    std::copy_n(data.c_str() + data.size() - left, n, buf);
    left -= n;
    if (left == 0) {
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return n;
  };
}

generator_cb deferred_generator() {
  return [](uint8_t *buf, size_t len, uint32_t *data_flags) {
    return NGHTTP2_ERR_DEFERRED;
  };
}

template <typename F, typename... T>
std::shared_ptr<Defer<F, T...>> defer_shared(F &&f, T &&... t) {
  return std::make_shared<Defer<F, T...>>(std::forward<F>(f),
                                          std::forward<T>(t)...);
}

generator_cb file_generator(const std::string &path) {
  auto fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    return generator_cb();
  }

  return file_generator_from_fd(fd);
}

generator_cb file_generator_from_fd(int fd) {
  auto d = defer_shared(close, fd);

  return [fd, d](uint8_t *buf, size_t len, uint32_t *data_flags)
      -> generator_cb::result_type {
        ssize_t n;
        while ((n = read(fd, buf, len)) == -1 && errno == EINTR)
          ;

        if (n == -1) {
          return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }

        if (n == 0) {
          *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        }

        return n;
      };
}

bool check_path(const std::string &path) { return util::check_path(path); }

std::string percent_decode(const std::string &s) {
  return util::percent_decode(std::begin(s), std::end(s));
}

std::string http_date(int64_t t) { return util::http_date(t); }

boost::system::error_code host_service_from_uri(boost::system::error_code &ec,
                                                std::string &scheme,
                                                std::string &host,
                                                std::string &service,
                                                const std::string &uri) {
  ec.clear();

  http_parser_url u{};
  if (http_parser_parse_url(uri.c_str(), uri.size(), 0, &u) != 0) {
    ec = make_error_code(boost::system::errc::invalid_argument);
    return ec;
  }

  if ((u.field_set & (1 << UF_SCHEMA)) == 0 ||
      (u.field_set & (1 << UF_HOST)) == 0) {
    ec = make_error_code(boost::system::errc::invalid_argument);
    return ec;
  }

  http2::copy_url_component(scheme, &u, UF_SCHEMA, uri.c_str());
  http2::copy_url_component(host, &u, UF_HOST, uri.c_str());

  if (u.field_set & (1 << UF_PORT)) {
    http2::copy_url_component(service, &u, UF_PORT, uri.c_str());
  } else {
    service = scheme;
  }

  return ec;
}

bool tls_h2_negotiated(ssl_socket &socket) {
  auto ssl = socket.native_handle();

  const unsigned char *next_proto = nullptr;
  unsigned int next_proto_len = 0;

  SSL_get0_next_proto_negotiated(ssl, &next_proto, &next_proto_len);
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
  if (next_proto == nullptr) {
    SSL_get0_alpn_selected(ssl, &next_proto, &next_proto_len);
  }
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L

  if (next_proto == nullptr) {
    return false;
  }

  return util::check_h2_is_selected(next_proto, next_proto_len);
}

} // namespace asio_http2
} // namespace nghttp2
