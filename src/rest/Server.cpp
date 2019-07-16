
#include "Server.h"

using namespace asiodemo;
using namespace asiodemo::rest;

#include <chrono>
#include <thread>

void Server::addHandler(std::string path, HandleFunc func) {
  _handlers.emplace(path, func);
}


void Server::listenAndServe(std::string host, int port) {

  _ioContext = std::make_shared<asio::io_context>(1);
  auto guard = asio::make_work_guard(*_ioContext));
  asio::io_context* ctx = _ioContext.get();

  std::thread t([ctx]() { ctx->run(); });

  if (_useTLS) {
    _acceptor = std::make_unique<AcceptorTcp<SocketType::Ssl>>(_context, *this);
  } else {
    _acceptor = std::make_unique<AcceptorTcp<SocketType::Tcp>>(_context, *this);
  }

  _acceptor->open();

  std::this_thread::sleep_for(std::chrono::seconds(120));

  _guard.reset();  // allow run() to exit
  t.join();
  _ioContext->stop();
}

std::unique_ptr<Response> Server::execute(Request const& req) {
   auto const& it = _handlers.find(req.path);
   if (it != _handlers.end()) {
     return it.second(req);
   }

  auto res = std::make_unique<Response>();
  res->status_code = ResponseCode::NOT_FOUND;
  res->body = std::make_unique<std::string>();
  res->body->append("Could not find the handler");
  return res;
}

asio::ssl::context& EventLoopService::sslContext() {
  std::lock_guard<std::mutex> guard(_sslContextMutex);
  if (!_sslContext) {
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    _sslContext.reset(new asio::ssl::context(asio::ssl::context::tls));
#else
    _sslContext.reset(new asio::ssl::context(asio::ssl::context::sslv23));
#endif
    _sslContext->set_default_verify_paths();
  }
  return *_sslContext;
}