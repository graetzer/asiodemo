////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef ASIOSOCKET_H
#define ASIOSOCKET_H 1

// make sure that IOCP is used on windows
#if defined(_WIN32) && !defined(_WIN32_WINNT)
// #define _WIN32_WINNT_VISTA  0x0600
#define _WIN32_WINNT 0x0600
#endif

#include <asio.hpp>
#include <asio/ssl.hpp>

namespace asiodemo { namespace rest {

enum class SocketType { Tcp = 1, Ssl = 2, Unix = 3 };

/// Wrapper class that contains sockets / ssl-stream
/// and the corrsponding peer endpoint
template <SocketType T>
struct AsioSocket {};

template <>
struct AsioSocket<SocketType::Tcp> {
  AsioSocket(asio::io_context& ctx) : context(ctx), socket(ctx), timer(ctx) {}

  ~AsioSocket() {
    timer.cancel();
    try {
      asio::error_code ec;
      shutdown(ec);
    } catch (...) {
    }
  }

  void setNonBlocking(bool v) { socket.non_blocking(v); }
  bool supportsMixedIO() const { return true; }
  std::size_t available(asio::error_code& ec) const {
    return socket.lowest_layer().available(ec);
  }

  void shutdown(asio::error_code& ec) {
    if (socket.is_open()) {
#ifndef _WIN32
      socket.cancel(ec);
#endif
      if (!ec) {
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
      }
      if (!ec || ec == asio::error::basic_errors::not_connected) {
        ec.clear();
        socket.close(ec);
      }
    }
  }

  asio::io_context& context;
  asio::ip::tcp::socket socket;
  asio::ip::tcp::acceptor::endpoint_type peer;
  asio::steady_timer timer;
  asio::streambuf buffer;
};

template <>
struct AsioSocket<SocketType::Ssl> {
  AsioSocket(asio::io_context& ctx, asio::ssl::context& sslContext)
      : context(ctx), socket(ctx, sslContext), timer(ctx) {}

  ~AsioSocket() {
    try {
      timer.cancel();
      asio::error_code ec;
      shutdown(ec);
    } catch (...) {
    }
  }

  void setNonBlocking(bool v) { socket.lowest_layer().non_blocking(v); }
  bool supportsMixedIO() const { return false; }
  std::size_t available(asio::error_code& ec) const {
    return 0;  // always disable
  }

  template <typename F>
  void handshake(F&& cb) {
    // Perform SSL handshake and verify the remote host's certificate.
    socket.next_layer().set_option(asio::ip::tcp::no_delay(true));
    socket.async_handshake(asio::ssl::stream_base::server, std::forward<F>(cb));
  }

  void shutdown(asio::error_code& ec) {
    if (socket.lowest_layer().is_open()) {
#ifndef _WIN32
      socket.lowest_layer().cancel(ec);
#endif
      if (!ec) {
        socket.shutdown(ec);
      }
#ifndef _WIN32
      if (!ec || ec == asio::error::basic_errors::not_connected) {
        ec.clear();
        socket.lowest_layer().close(ec);
      }
#endif
    }
  }

  asio::io_context& context;
  asio::ssl::stream<asio::ip::tcp::socket> socket;
  asio::ip::tcp::acceptor::endpoint_type peer;
  asio::steady_timer timer;
  asio::streambuf buffer;
};

}}  // namespace asiodemo::rest
#endif
