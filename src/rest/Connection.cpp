////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#include "Connection.h"
#include "Server.h"
#include "Utils.h"

#include <cstring>
#include <iostream>

using namespace asiodemo;
using namespace asiodemo::rest;

namespace {
constexpr static size_t MaximalBodySize = 1024 * 1024 * 1024;  // 1024 MB

}  // namespace

template <SocketType T>
int Connection<T>::on_message_began(llhttp_t* p) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  self->_lastHeaderField.clear();
  self->_lastHeaderValue.clear();
  self->_origin.clear();
  self->_request = std::make_unique<rest::Request>();
  self->_lastHeaderWasValue = false;
  self->_shouldKeepAlive = false;
  self->_denyCredentials = false;
  
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_url(llhttp_t* p, const char* at, size_t len) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  self->_request->parseUrl(at, len);
  self->_request->method = utils::llhttpToRequestType(p);
  if (self->_request->method == Request::Type::ILLEGAL) {
    self->addSimpleResponse(rest::ResponseCode::METHOD_NOT_ALLOWED);
    return HPE_USER;
  }
  
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_status(llhttp_t* p, const char* at, size_t len) {
  // should not be used
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_header_field(llhttp_t* p, const char* at, size_t len) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  if (self->_lastHeaderWasValue) {
    utils::tolowerInPlace(self->_lastHeaderField);
    self->_request->setHeader(std::move(self->_lastHeaderField),
                                std::move(self->_lastHeaderValue));
    self->_lastHeaderField.assign(at, len);
  } else {
    self->_lastHeaderField.append(at, len);
  }
  self->_lastHeaderWasValue = false;
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_header_value(llhttp_t* p, const char* at, size_t len) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  if (self->_lastHeaderWasValue) {
    self->_lastHeaderValue.append(at, len);
  } else {
    self->_lastHeaderValue.assign(at, len);
  }
  self->_lastHeaderWasValue = true;
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_header_complete(llhttp_t* p) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  if (!self->_lastHeaderField.empty()) {
    utils::tolowerInPlace(self->_lastHeaderField);
    self->_request->setHeader(std::move(self->_lastHeaderField),
                                std::move(self->_lastHeaderValue));
  }

  if ((p->http_major != 1 && p->http_minor != 0) &&
      (p->http_major != 1 && p->http_minor != 1)) {
    self->addSimpleResponse(rest::ResponseCode::HTTP_VERSION_NOT_SUPPORTED);
    return HPE_USER;
  }
  if (p->content_length > MaximalBodySize) {
    self->addSimpleResponse(rest::ResponseCode::REQUEST_ENTITY_TOO_LARGE);
    return HPE_USER;
  }
  if (p->content_length > 0) {
    // lets not reserve more than 64MB at once
    uint64_t maxReserve = std::min<uint64_t>(2 << 26, p->content_length);
    self->_request->body.reserve(maxReserve + 1);
  }
  self->_shouldKeepAlive = llhttp_should_keep_alive(p);

  bool found;
  std::string const& expect = self->_request->header("expect", found);
  if (found && utils::trim(expect) == "100-continue") {
    std::cout
        << "received a 100-continue request";
    char const* response = "HTTP/1.1 100 Continue\r\n\r\n";
    auto buff = asio::buffer(response, strlen(response));
    asio::async_write(self->_protocol->socket, buff,
                         [self](asio::error_code const& ec, std::size_t transferred) {
                           llhttp_resume(&self->_parser);
                           self->asyncReadSome();
                         });
    return HPE_PAUSED;
  }
  if (self->_request->method == Request::Type::HEAD) {
    // Assume that request/response has no body, proceed parsing next message
    return 1; // 1 is defined by parser
  }
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_body(llhttp_t* p, const char* at, size_t len) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  self->_request->body.append(at, len);
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_message_complete(llhttp_t* p) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  self->processRequest();
  return HPE_PAUSED;
}

template <SocketType T>
Connection<T>::Connection(Server& server, std::unique_ptr<AsioSocket<T>> so)
  : _server(server), _protocol(std::move(so)) {
  // initialize http parsing code
  llhttp_settings_init(&_parserSettings);
  _parserSettings.on_message_begin = Connection<T>::on_message_began;
  _parserSettings.on_url = Connection<T>::on_url;
  _parserSettings.on_status = Connection<T>::on_status;
  _parserSettings.on_header_field = Connection<T>::on_header_field;
  _parserSettings.on_header_value = Connection<T>::on_header_value;
  _parserSettings.on_headers_complete = Connection<T>::on_header_complete;
  _parserSettings.on_body = Connection<T>::on_body;
  _parserSettings.on_message_complete = Connection<T>::on_message_complete;
  llhttp_init(&_parser, HTTP_REQUEST, &_parserSettings);
  _parser.data = this;
}

template <SocketType T>
Connection<T>::~Connection() {}

template <SocketType T>
void Connection<T>::start() {
  _protocol->setNonBlocking(true);
  asyncReadSome();
}

template <SocketType T>
void Connection<T>::close() {
  if (_protocol) {
    _protocol->timer.cancel();
    asio::error_code ec;
    _protocol->shutdown(ec);
    if (ec) {
      std::cout << "error shutting down asio socket: '" << ec.message() << "'";
    }
  }
}

template <SocketType T>
void Connection<T>::asyncReadSome() {
  asio::error_code ec;
  // first try a sync read for performance
  if (_protocol->supportsMixedIO()) {
    std::size_t available = _protocol->available(ec);
    while (!ec && available > 8) {
      auto mutableBuff = _protocol->buffer.prepare(available);
      size_t nread = _protocol->socket.read_some(mutableBuff, ec);
      _protocol->buffer.commit(nread);
      if (ec) {
        break;
      }
      if (!readCallback(ec)) {
        return;
      }
      available = _protocol->available(ec);
    }
    if (ec == asio::error::would_block) {
      ec.clear();
    }
  }
  
  // read pipelined requests / remaining data
  if (_protocol->buffer.size() > 0 && !readCallback(ec)) {
    return;
  }
  
  auto cb = [self = this->shared_from_this()](asio::error_code const& ec,
                                              size_t transferred) {
    auto* thisPtr = static_cast<Connection<T>*>(self.get());
    thisPtr->_protocol->buffer.commit(transferred);
    if (thisPtr->readCallback(ec)) {
      thisPtr->asyncReadSome();
    }
  };
  auto mutableBuff = _protocol->buffer.prepare(READ_BLOCK_SIZE);
  _protocol->socket.async_read_some(mutableBuff, std::move(cb));
}

template <SocketType T>
bool Connection<T>::readCallback(asio::error_code ec) {

  llhttp_errno_t err;
  if (ec) { // got a connection error
    if (ec == asio::error::misc_errors::eof) {
      err = llhttp_finish(&_parser);
    } else {
      std::cout << "Error while reading from socket: '" << ec.message() << "'";
      err = HPE_CLOSED_CONNECTION;
    }
  } else { // Inspect the received data
    
    size_t parsedBytes = 0;
    for (auto const& buffer : this->_protocol->buffer.data()) {
      const char* data = reinterpret_cast<const char*>(buffer.data());
      err = llhttp_execute(&_parser, data, buffer.size());
      if (err != HPE_OK) {
        parsedBytes += llhttp_get_error_pos(&_parser) - data;
        break;
      }
      parsedBytes += buffer.size();
    }
    
    assert(parsedBytes < std::numeric_limits<size_t>::max());
    // Remove consumed data from receive buffer.
    this->_protocol->buffer.consume(parsedBytes);
    
    if (err == HPE_PAUSED_UPGRADE) {
      this->addSimpleResponse(rest::ResponseCode::NOT_IMPLEMENTED);
    }
  }

  if (err != HPE_OK && err != HPE_USER && err != HPE_PAUSED) {
    std::cout << "HTTP parse failure: '" << llhttp_get_error_reason(&_parser) << "'";
    this->close();
  }

  return err == HPE_OK && !ec;
}

template <SocketType T>
void Connection<T>::processRequest() {
  assert(_request);
  
  this->_protocol->timer.cancel();
  
  std::cout
      << "\"http-request-begin\",\"" << (void*)this << "\",\""
      << _protocol->peer.address().to_string() << "\",\""
      << "\"";

  parseOriginHeader(*_request);

  // OPTIONS requests currently go unauthenticated
  if (_request->method == rest::Request::Type::OPTIONS) {
    processCorsOptions();
    return;
  }

  // TODO scrape authentication, etc
  
  auto response = _server.execute(*_request);

  sendResponse(std::move(response));
}


/// @brief send error response including response body
template <SocketType T>
void Connection<T>::addSimpleResponse(rest::ResponseCode code) {
  try {
    auto resp = std::make_unique<Response>();
    resp->status_code = code;
    sendResponse(std::move(resp));
  } catch (...) {
    std::cout << "addSimpleResponse received an exception, closing connection";
    this->close();
  }
}

template <SocketType T>
void Connection<T>::parseOriginHeader(rest::Request const& req) {
  // handle origin headers
  _origin = req.header("origin");
  if (!_origin.empty()) {
    // default is to allow nothing
    _denyCredentials = true;

    // if the request asks to allow credentials, we'll check against the
    // configured whitelist of origins
    std::vector<std::string> accessControlAllowOrigins{"arangodb.com",
      "example.com"
    };

    if (!accessControlAllowOrigins.empty()) {
      if (accessControlAllowOrigins[0] == "*") {
        // special case: allow everything
        _denyCredentials = false;
      } else if (!_origin.empty()) {
        // copy origin string
        if (_origin[_origin.size() - 1] == '/') {
          // strip trailing slash
          auto result = std::find(accessControlAllowOrigins.begin(),
                                  accessControlAllowOrigins.end(),
                                  _origin.substr(0, _origin.size() - 1));
          _denyCredentials = (result == accessControlAllowOrigins.end());
        } else {
          auto result = std::find(accessControlAllowOrigins.begin(),
                                  accessControlAllowOrigins.end(), _origin);
          _denyCredentials = (result == accessControlAllowOrigins.end());
        }
      } else {
        assert(_denyCredentials);
      }
    }
  }
}

/// handle an OPTIONS request
template <SocketType T>
void Connection<T>::processCorsOptions() {
  auto resp = std::make_unique<Response>();
  resp->status_code = rest::ResponseCode::OK;

  // send back which HTTP methods are allowed for the resource
  // we'll allow all
  std::string allowedMethods = "DELETE, GET, HEAD, OPTIONS, PATCH, POST, PUT";
  resp->setHeaderNCIfNotSet("Allow", allowedMethods);

  if (!_origin.empty()) {
    std::cout << "got CORS preflight request";
    std::string const allowHeaders = utils::trim(_request->header("access-control-request-headers"));

    resp->setHeaderNCIfNotSet("access-control-allow-methods", allowedMethods);

    if (!allowHeaders.empty()) {
      // allow all extra headers the client requested
      // we don't verify them here. the worst that can happen is that the
      // client sends some broken headers and then later cannot access the data
      // on the server. that's a client problem.
      resp->setHeaderNCIfNotSet("access-control-allow-headers", allowHeaders);

      std::cout << "client requested validation of the following headers: " << allowHeaders;
    }

    // set caching time (hard-coded value)
    resp->setHeaderNCIfNotSet("access-control-max-age", "1800");
  }

  _request.reset(); // forge the request
  sendResponse(std::move(resp));
}

template <SocketType T>
void Connection<T>::sendResponse(std::unique_ptr<Response> response) {

  // CORS response handling
  if (!_origin.empty()) {
    // the request contained an Origin header. We have to send back the
    // access-control-allow-origin header now
    std::cout << "handling CORS response";

    // send back original value of "Origin" header
    response->setHeaderNCIfNotSet("access-control-allow-origin", _origin);

    // send back "Access-Control-Allow-Credentials" header
    response->setHeaderNCIfNotSet("access-control-allow-credentials",
                                 (_denyCredentials ? "false" : "true"));

    // use "IfNotSet" here because we should not override HTTP headers set
    // by Foxx applications
    response->setHeaderNCIfNotSet("access-control-expose-headers",
                                 "etag, content-encoding, content-length, location, server");
  }

  std::unique_ptr<std::string> header = response->generateHeader();
  std::unique_ptr<std::string> body = std::move(response->body);
  std::array<asio::const_buffer, 2> buffers;
  buffers[0] = asio::buffer(header->data(), header->size());
  if (HTTP_HEAD != _parser.method) {
    buffers[1] = asio::buffer(body->data(), body->size());
  }

  // FIXME measure performance w/o sync write
  auto cb = [self = this->shared_from_this(),
             h = std::move(header),
             b = std::move(body)](asio::error_code ec,
                                  size_t nwrite) {
    auto* thisPtr = static_cast<Connection<T>*>(self.get());
    
    llhttp_errno_t err = llhttp_get_errno(&thisPtr->_parser);
    if (ec || !thisPtr->_shouldKeepAlive || err != HPE_PAUSED) {
      if (ec) {
        std::cout << "asio write error: '" << ec.message() << "'";
      }
      thisPtr->close();
    } else {  // ec == HPE_PAUSED
      llhttp_resume(&thisPtr->_parser);
      thisPtr->asyncReadSome();
    }
  };
  asio::async_write(this->_protocol->socket, std::move(buffers), std::move(cb));

  // turn on the keepAlive timer
  this->_protocol->timer.expires_after(std::chrono::seconds(60));
  this->_protocol->timer.async_wait([this](asio::error_code ec) {
    if (!ec) {
      std::cout << "keep alive timout, closing stream!";
      this->close();
    }
  });
}

template class asiodemo::rest::Connection<SocketType::Tcp>;
template class asiodemo::rest::Connection<SocketType::Ssl>;
//#ifndef _WIN32
//template class asiodemo::rest::Connection<SocketType::Unix>;
//#endif
