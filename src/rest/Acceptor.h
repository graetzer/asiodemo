////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef ACCEPTORTCP_H
#define ACCEPTORTCP_H 1

#include "AsioSocket.h"

namespace asiodemo
{
namespace rest
{

template <SocketType T>
class Acceptor
{
public:
  Acceptor(asio::io_context &ctx) : _ctx(ctx), _acceptor(ctx), _open(false), _asioSocket(), _acceptFailures(0) {}

public:
  void open();
  void close();
  void asyncAccept();

private:
  void performHandshake(std::unique_ptr<AsioSocket<T>>);

  void handleError(asio::error_code const &);
  static constexpr int maxAcceptErrors = 128;

private:
  asio::io_context &_ctx;
  asio::ip::tcp::acceptor _acceptor;
  std::unique_ptr<AsioSocket<T>> _asioSocket;

  bool _open;
  size_t _acceptFailures;
};
} // namespace rest
} // namespace asiodemo

#endif
