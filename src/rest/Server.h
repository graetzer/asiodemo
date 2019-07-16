

////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef SERVER_H
#define SERVER_H 1

#include <memory>
#include <string>
#include <thread>

#include "Acceptor.h"
#include "Request.h"
#include "Response.h"

namespace asiodemo { namespace rest {

class Server {
  typedef std::function<std::unique_ptr<Response>(Request const&)> HandleFunc;

 public:
  Server() {}
  ~Server() {}

  void addHandler(std::string path, HandleFunc);

  void listenAndServe();

  asio::ssl::context& sslContext();

  std::unique_ptr<Response> execute(Request const&);

 private:
  std::map<std::string, HandleFunc> _handlers;

  /// protect ssl context creation
  std::mutex _sslContextMutex;
  /// global SSL context to use here
  std::unique_ptr<asio::ssl::context> _sslContext;

  /// io contexts
  std::shared_ptr<asio::io_context> _ioContext;
};

}}  // namespace asiodemo::rest

#endif
