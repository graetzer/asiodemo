////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef ACCEPTORTCP_H
#define ACCEPTORTCP_H 1

#include "AsioSocket.h"

namespace asiodemo {
namespace rest {

class Server;

class Acceptor {
public:
  virtual ~Acceptor() {}

  virtual void open() = 0;
  virtual void close() = 0;

  /// start accepting connections
  virtual void asyncAccept() = 0;

  protected:

    Acceptor(rest::Server& server) : _open(false), _acceptFailures(0), _server(server) {}

  protected:

    bool _open;
    size_t _acceptFailures;
    rest::Server& _server;
};

template <SocketType T>
class AcceptorTcp : public Acceptor {
public:
  AcceptorTcp(asio::io_context &ctx, rest::Server& server);

public:
  void open() override;
  void close() override;
  void asyncAccept() override;

private:
  void performHandshake(std::unique_ptr<AsioSocket<T>>);

  void handleError(asio::error_code const &);
  static constexpr int maxAcceptErrors = 128;

private:
  asio::io_context &_ctx;
  asio::ip::tcp::acceptor _acceptor;
  std::unique_ptr<AsioSocket<T>> _asioSocket;
};

} // namespace rest
} // namespace asiodemo

#endif
