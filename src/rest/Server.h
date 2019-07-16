

////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef SERVER_H
#define SERVER_H 1

#include <thread>
#include <string>
#include <memory>

#include "Acceptor.h"
#include "Request.h"
#include "Response.h"

namespace asiodemo {
namespace rest {

class Server {

  typedef std::function<std::unique_ptr<Response>(Request const&)> HandleFunc;

  public:

    Server() {}

    void addHandler(std::string path, HandleFunc);

    bool useTLS(bool yesNo) {
      _useTLS = yesNo;
    }

    void listenAndServe(std::string host, int port);

    asio::ssl::context& sslContext();

    std::unique_ptr<Response> perform(Request const&);

  private:
    bool _useTLS = false;
    std::map<std::string, HandleFunc> _handlers;
    
    std::unique_ptr<Acceptor> _acceptor;

    /// protect ssl context creation
    std::mutex _sslContextMutex;
    /// global SSL context to use here
    std::unique_ptr<asio::ssl::context> _sslContext;

    /// io contexts
    std::shared_ptr<asio::io_context> _ioContext;
    /// Used to keep the io-context alive.
    asio::io_context::executor_type _guard;
};

} namespace rest
} // namespace asiodemo 

#endif