

////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef CONNECTION_H
#define CONNECTION_H 1

#include "AsioSocket.h"
#include "Request.h"
#include "Response.h"

#include <llhttp.h>

namespace asiodemo {
namespace rest {

class Server;
class Request;

template<SocketType T>
class Connection : std::enable_shared_from_this<Connection<T>>  {
 public:
  Connection(Server& server, 
             std::unique_ptr<AsioSocket<T>>);

  ~Connection();

  void start();
  void close();
  
 private:
  static int on_message_began(llhttp_t* p);
  static int on_url(llhttp_t* p, const char* at, size_t len);
  static int on_status(llhttp_t* p, const char* at, size_t len);
  static int on_header_field(llhttp_t* p, const char* at, size_t len);
  static int on_header_value(llhttp_t* p, const char* at, size_t len);
  static int on_header_complete(llhttp_t* p);
  static int on_body(llhttp_t* p, const char* at, size_t len);
  static int on_message_complete(llhttp_t* p);

 private:
  
  /// read from socket
  void asyncReadSome();

  /// default max chunksize is 30kb in arangodb (each read fits)
  static constexpr size_t READ_BLOCK_SIZE = 1024 * 32;
  
  void sendResponse(std::unique_ptr<rest::Response> response);

  /// @brief send error response including response body
  void addSimpleResponse(rest::ResponseCode);
  
  bool readCallback(asio::error_code ec);

  void processRequest();

  void parseOriginHeader(rest::Request const& req);
  /// handle an OPTIONS request
  void processCorsOptions();
  /// check authentication headers
  //ResponseCode handleAuthHeader(rest::Request& request);
  /// decompress content
  bool handleContentEncoding(rest::Request&);
  
 private:
  rest::Server& _server;
  std::unique_ptr<AsioSocket<T>> _protocol;

    /// the node http-parser
  llhttp_t _parser;
  llhttp_settings_t _parserSettings;

  // ==== parser state ====
  std::string _lastHeaderField;
  std::string _lastHeaderValue;
  std::string _origin;  // value of the HTTP origin header the client sent (if
  std::unique_ptr<Request> _request;
  bool _lastHeaderWasValue;
  bool _shouldKeepAlive;  /// keep connection open
  bool _denyCredentials;  /// credentialed requests or not (only CORS)

  bool _checkedVstUpgrade;
};

}  // namespace rest
}  // namespace arangodb

#endif
