////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#include "Acceptor.h"
#include "Connection.h"
#include "Server.h"

#include <iostream>
#include <chrono>

using namespace asiodemo::rest;

template <SocketType T>
AcceptorTcp<T>::AcceptorTcp(asio::io_context &ctx, rest::Server& server)
  : Acceptor(server), _ctx(ctx), _acceptor(ctx), _asioSocket() {}

template <SocketType T>
void AcceptorTcp<T>::open() {
  asio::ip::tcp::resolver resolver(_ctx);

  std::string hostname = "0.0.0.0";
  int portNumber = 80;

  asio::ip::tcp::endpoint asioEndpoint;
  asio::error_code ec;
  auto address = asio::ip::address::from_string(hostname, ec);
  if (!ec) {
    asioEndpoint = asio::ip::tcp::endpoint(address, portNumber);
  } else {  // we need to resolve the string containing the ip
    std::unique_ptr<asio::ip::tcp::resolver::query> query;
    // if (_endpoint->domain() == AF_INET6) {
    //   query.reset(new asio::ip::tcp::resolver::query(asio::ip::tcp::v6(), hostname,
    //                                                     std::to_string(portNumber)));
    // } else if (_endpoint->domain() == AF_INET) {
      query.reset(new asio::ip::tcp::resolver::query(asio::ip::tcp::v4(), hostname,
                                                        std::to_string(portNumber)));
    // } else {
    //   THROW_ARANGO_EXCEPTION(TRI_ERROR_IP_ADDRESS_INVALID);
    // }

    asio::ip::tcp::resolver::iterator iter = resolver.resolve(*query, ec);
    if (ec) {
      std::cout
          << "unable to to resolve endpoint ' " << hostname << ":" << portNumber
          << "': " << ec.message();
      throw std::runtime_error(ec.message());
    }

    if (asio::ip::tcp::resolver::iterator{} == iter) {
      std::cout
          << "unable to to resolve endpoint: endpoint is default constructed";
    }

    asioEndpoint = iter->endpoint();  // function not documented in boost?!
  }
  _acceptor.open(asioEndpoint.protocol());

#ifdef _WIN32
  // on Windows everything is different of course:
  // we need to set SO_EXCLUSIVEADDRUSE to prevent others from binding to our
  // ip/port.
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms740621(v=vs.85).aspx
  BOOL trueOption = 1;

  if (::setsockopt(_acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   (char const*)&trueOption, sizeof(BOOL)) != 0) {
    std::cout
        << "unable to set acceptor socket option: " << WSAGetLastError();
    THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_FAILED,
                                   "unable to set acceptor socket option");
  }
#else
  _acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
#endif

  _acceptor.bind(asioEndpoint, ec);
  if (ec) {
    std::cout
          << "unable to to bind to endpoint ' " << hostname << ":" << portNumber
        << "': " << ec.message();
    throw std::runtime_error(ec.message());
  }

  _acceptor.listen(8, ec);
  if (ec) {
    std::cout
        << "unable to listen to endpoint '" << hostname << ":" << portNumber
        << ": " << ec.message();
    throw std::runtime_error(ec.message());
  }
  _open = true;
  
  std::cout << "successfully opened acceptor TCP";
  
  asyncAccept();
}

template <SocketType T>
void AcceptorTcp<T>::close() {
  if (_asioSocket) {
    _asioSocket->timer.cancel();
  }
  if (_open) {
    _acceptor.close();
    if (_asioSocket) {
      asio::error_code ec;
      _asioSocket->shutdown(ec);
    }
  }
  _open = false;
}

template <>
void AcceptorTcp<SocketType::Tcp>::asyncAccept() {
  assert(!_asioSocket);

  // one could choose another IO context here to scale up 
// auto& ctx = ::selectIoContext();
  _asioSocket.reset(new AsioSocket<SocketType::Tcp>(_ctx));
  auto handler = [this](asio::error_code const& ec) {
    if (ec) {
      handleError(ec);
      return;
    }

    std::unique_ptr<AsioSocket<SocketType::Tcp>> as = std::move(_asioSocket);
    auto conn = std::make_shared<Connection<SocketType::Tcp>>(_server, std::move(as));
    conn->start();

    // accept next request
    this->asyncAccept();
  };

  _acceptor.async_accept(_asioSocket->socket, _asioSocket->peer, std::move(handler));
}

template <>
void AcceptorTcp<SocketType::Tcp>::performHandshake(std::unique_ptr<AsioSocket<SocketType::Tcp>> proto) {
  assert(false); // MSVC requires the implementation to exist
}

template <>
void AcceptorTcp<SocketType::Ssl>::performHandshake(std::unique_ptr<AsioSocket<SocketType::Ssl>> proto) {
  // io_context is single-threaded, no sync needed
  auto* ptr = proto.get();
  proto->timer.expires_from_now(std::chrono::seconds(60));
  proto->timer.async_wait([ptr](asio::error_code const& ec) {
    if (ec) { // canceled
      return;
    }
    asio::error_code err;
    ptr->shutdown(err); // ignore error
  });
  
  auto cb = [this, as = std::move(proto)](asio::error_code const& ec) mutable {
    as->timer.cancel();
    if (ec) {
      std::cout
      << "error during TLS handshake: '" << ec.message() << "'";
      asio::error_code err;
      as->shutdown(err); // ignore error
      return;
    }
    
    auto conn =
    std::make_unique<Connection<SocketType::Ssl>>(_server, std::move(as));
    conn->start();
  };
  ptr->handshake(std::move(cb));
}

template <>
void AcceptorTcp<SocketType::Ssl>::asyncAccept() {
  assert(!_asioSocket);

  // select the io context for this socket
  auto& ctx = _ctx; // _server.selectIoContext();

  _asioSocket = std::make_unique<AsioSocket<SocketType::Ssl>>(ctx, _server.sslContext());
  auto handler = [this](asio::error_code const& ec) {
    if (ec) {
      handleError(ec);
      return;
    }
    
    performHandshake(std::move(_asioSocket));
    this->asyncAccept();
  };

  _acceptor.async_accept(_asioSocket->socket.lowest_layer(), _asioSocket->peer,
                         std::move(handler));
}

template <SocketType T>
void AcceptorTcp<T>::handleError(asio::error_code const& ec) {
  if (ec == asio::error::operation_aborted) {
    // this "error" is accpepted, so it doesn't justify a warning
    std::cout
        << "accept failed: " << ec.message();
    return;
  }

  if (++_acceptFailures <= maxAcceptErrors) {
    std::cout
        << "accept failed: " << ec.message();
    if (_acceptFailures == maxAcceptErrors) {
      std::cout
          << "too many accept failures, stopping to report";
    }
  }
  asyncAccept();  // retry
  return;
}


template class asiodemo::rest::AcceptorTcp<SocketType::Tcp>;
template class asiodemo::rest::AcceptorTcp<SocketType::Ssl>;
