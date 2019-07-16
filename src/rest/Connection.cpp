////////////////////////////////////////////////////////////////////////////////
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#include "Connection.h"

#include <cstring>

using namespace asiodemo;
using namespace asiodemo::rest;

namespace {
constexpr static size_t MaximalBodySize = 1024 * 1024 * 1024;  // 1024 MB
rest::Request::Type llhttpToRequestType(llhttp_t* p) {
  switch (p->method) {
    case HTTP_DELETE:
      return Request::Type::DELETE_REQ;
    case HTTP_GET:
      return RequestType::GET;
    case HTTP_HEAD:
      return RequestType::HEAD;
    case HTTP_POST:
      return RequestType::POST;
    case HTTP_PUT:
      return RequestType::PUT;
    case HTTP_OPTIONS:
      return RequestType::OPTIONS;
    case HTTP_PATCH:
      return RequestType::PATCH;
    default:
      return RequestType::ILLEGAL;
  }
}
}  // namespace

template <SocketType T>
int Connection<T>::on_message_began(llhttp_t* p) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  self->_lastHeaderField.clear();
  self->_lastHeaderValue.clear();
  self->_origin.clear();
  self->_request =
      std::make_unique<HttpRequest>(self->_connectionInfo, /*header*/ nullptr, 0,
                                    /*allowMethodOverride*/ true);
  self->_lastHeaderWasValue = false;
  self->_shouldKeepAlive = false;
  self->_denyCredentials = false;
  
  // acquire a new statistics entry for the request
  RequestStatistics* stat = self->acquireStatistics(1UL);
  RequestStatistics::SET_READ_START(stat, TRI_microtime());
  
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_url(llhttp_t* p, const char* at, size_t len) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  self->_request->parseUrl(at, len);
  self->_request->setRequestType(llhttpToRequestType(p));
  if (self->_request->requestType() == RequestType::ILLEGAL) {
    self->addSimpleResponse(rest::ResponseCode::METHOD_NOT_ALLOWED,
                            rest::ContentType::UNSET, 1, VPackBuffer<uint8_t>());
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
    StringUtils::tolowerInPlace(&self->_lastHeaderField);
    self->_request->setHeaderV2(std::move(self->_lastHeaderField),
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
    StringUtils::tolowerInPlace(&self->_lastHeaderField);
    self->_request->setHeaderV2(std::move(self->_lastHeaderField),
                                std::move(self->_lastHeaderValue));
  }

  if ((p->http_major != 1 && p->http_minor != 0) &&
      (p->http_major != 1 && p->http_minor != 1)) {
    self->addSimpleResponse(rest::ResponseCode::HTTP_VERSION_NOT_SUPPORTED,
                            rest::ContentType::UNSET, 1, VPackBuffer<uint8_t>());
    return HPE_USER;
  }
  if (p->content_length > MaximalBodySize) {
    self->addSimpleResponse(rest::ResponseCode::REQUEST_ENTITY_TOO_LARGE,
                            rest::ContentType::UNSET, 1, VPackBuffer<uint8_t>());
    return HPE_USER;
  }
  if (p->content_length > 0) {
    // lets not reserve more than 64MB at once
    uint64_t maxReserve = std::min<uint64_t>(2 << 26, p->content_length);
    self->_request->body().reserve(maxReserve + 1);
  }
  self->_shouldKeepAlive = llhttp_should_keep_alive(p);

  bool found;
  std::string const& expect = self->_request->header(StaticStrings::Expect, found);
  if (found && StringUtils::trim(expect) == "100-continue") {
    LOG_TOPIC("2b604", TRACE, arangodb::Logger::REQUESTS)
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
  if (self->_request->requestType() == RequestType::HEAD) {
    // Assume that request/response has no body, proceed parsing next message
    return 1; // 1 is defined by parser
  }
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_body(llhttp_t* p, const char* at, size_t len) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  self->_request->body().append(at, len);
  return HPE_OK;
}

template <SocketType T>
int Connection<T>::on_message_complete(llhttp_t* p) {
  Connection<T>* self = static_cast<Connection<T>*>(p->data);
  
  RequestStatistics* stat = self->statistics(1UL);
  RequestStatistics::SET_READ_END(stat);
  RequestStatistics::ADD_RECEIVED_BYTES(stat, self->_request->body().size());
  
  self->processRequest();
  return HPE_PAUSED;
}

template <SocketType T>
Connection<T>::Connection(std::unique_ptr<AsioSocket<T>> so)
  : _protocol(std::move(so)) {
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

/// @brief send error response including response body
template <SocketType T>
void Connection<T>::addSimpleResponse(rest::ResponseCode code,
                                        rest::ContentType respType, uint64_t /*messageId*/,
                                        velocypack::Buffer<uint8_t>&& buffer) {
  try {
    auto resp = std::make_unique<Response>(code, /*buffer*/nullptr);
    resp->setContentType(respType);
    if (!buffer.empty()) {
      resp->setPayload(std::move(buffer), true, VPackOptions::Defaults);
    }
    sendResponse(std::move(resp), this->stealStatistics(1UL));
  } catch (...) {
    std::cout << "addSimpleResponse received an exception, closing connection";
    this->close();
  }
}

template <SocketType T>
bool Connection<T>::readCallback(asio::error_code ec) {

  llhttp_errno_t err;
  if (ec) { // got a connection error
    if (ec == asio::error::misc_errors::eof) {
      err = llhttp_finish(&_parser);
    } else {
      LOG_TOPIC("395fe", DEBUG, Logger::REQUESTS)
          << "Error while reading from socket: '" << ec.message() << "'";
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
      this->addSimpleResponse(rest::ResponseCode::NOT_IMPLEMENTED,
                              rest::ContentType::UNSET, 1, VPackBuffer<uint8_t>());
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
      << as->peer.address().to_string() << "\",\""
      << "\"";

  parseOriginHeader(*_request);

  // OPTIONS requests currently go unauthenticated
  if (_request->requestType() == rest::RequestType::OPTIONS) {
    processCorsOptions();
    return;
  }

  // scrape the auth headers to determine and authenticate the user
  rest::ResponseCode authResult = handleAuthHeader(*_request);

  // authenticated
  if (authResult == rest::ResponseCode::SERVER_ERROR) {
    std::string realm = "Bearer token_type=\"JWT\", realm=\"ArangoDB\"";
    auto res = std::make_unique<Response>(rest::ResponseCode::UNAUTHORIZED, nullptr);
    res->setHeaderNC(StaticStrings::WwwAuthenticate, std::move(realm));
    sendResponse(std::move(res), nullptr);
    return;
  }

  // first check whether we allow the request to continue
  CommTask::Flow cont = this->prepareExecution(*_request);
  if (cont != CommTask::Flow::Continue) {
    return;  // prepareExecution sends the error message
  }

  // unzip / deflate
  if (!handleContentEncoding(*_request)) {
    this->addErrorResponse(rest::ResponseCode::BAD, _request->contentTypeResponse(), 1,
                     TRI_ERROR_BAD_PARAMETER, "decoding error");
    return;
  }

  // create a handler and execute
  auto resp = std::make_unique<Response>(rest::ResponseCode::SERVER_ERROR, nullptr);
  resp->setContentType(_request->contentTypeResponse());
  resp->setContentTypeRequested(_request->contentTypeResponse());

  this->executeRequest(std::move(_request), std::move(resp));
}

template <SocketType T>
void Connection<T>::parseOriginHeader(HttpRequest const& req) {
  // handle origin headers
  _origin = req.header(StaticStrings::Origin);
  if (!_origin.empty()) {
    // default is to allow nothing
    _denyCredentials = true;

    // if the request asks to allow credentials, we'll check against the
    // configured whitelist of origins
    std::vector<std::string> const& accessControlAllowOrigins =
        GeneralServerFeature::accessControlAllowOrigins();

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
  auto resp = std::make_unique<Response>(rest::ResponseCode::OK, nullptr);

  resp->setHeaderNCIfNotSet(StaticStrings::Allow, StaticStrings::CorsMethods);

  if (!_origin.empty()) {
    LOG_TOPIC("e1cfa", DEBUG, arangodb::Logger::REQUESTS)
        << "got CORS preflight request";
    std::string const allowHeaders =
        StringUtils::trim(_request->header("access-control-request-headers"));

    // send back which HTTP methods are allowed for the resource
    // we'll allow all
    resp->setHeaderNCIfNotSet(StaticStrings::AccessControlAllowMethods,
                              StaticStrings::CorsMethods);

    if (!allowHeaders.empty()) {
      // allow all extra headers the client requested
      // we don't verify them here. the worst that can happen is that the
      // client sends some broken headers and then later cannot access the data
      // on the server. that's a client problem.
      resp->setHeaderNCIfNotSet(StaticStrings::AccessControlAllowHeaders, allowHeaders);

      LOG_TOPIC("55413", TRACE, arangodb::Logger::REQUESTS)
          << "client requested validation of the following headers: " << allowHeaders;
    }

    // set caching time (hard-coded value)
    resp->setHeaderNCIfNotSet(StaticStrings::AccessControlMaxAge, StaticStrings::N1800);
  }

  _request.reset(); // forge the request
  sendResponse(std::move(resp), nullptr);
}

template <SocketType T>
ResponseCode Connection<T>::handleAuthHeader(HttpRequest& req) {
  bool found;
  std::string const& authStr = req.header(StaticStrings::Authorization, found);
  if (!found) {
    if (this->_auth->isActive()) {
      events::CredentialsMissing(req);
      return rest::ResponseCode::UNAUTHORIZED;
    }
    return rest::ResponseCode::OK;
  }

  size_t methodPos = authStr.find_first_of(' ');
  if (methodPos != std::string::npos) {
    // skip over authentication method
    char const* auth = authStr.c_str() + methodPos;
    while (*auth == ' ') {
      ++auth;
    }

    if (Logger::logRequestParameters()) {
      LOG_TOPIC("c4536", DEBUG, arangodb::Logger::REQUESTS)
          << "\"authorization-header\",\"" << (void*)this << "\",\"" << authStr << "\"";
    }

    try {
      AuthenticationMethod authMethod = AuthenticationMethod::NONE;
      if (authStr.size() >= 6) {
        if (strncasecmp(authStr.c_str(), "basic ", 6) == 0) {
          authMethod = AuthenticationMethod::BASIC;
        } else if (strncasecmp(authStr.c_str(), "bearer ", 7) == 0) {
          authMethod = AuthenticationMethod::JWT;
        }
      }

      req.setAuthenticationMethod(authMethod);
      if (authMethod != AuthenticationMethod::NONE) {
        this->_authToken = this->_auth->tokenCache().checkAuthentication(authMethod, auth);
        req.setAuthenticated(this->_authToken.authenticated());
        req.setUser(this->_authToken._username);  // do copy here, so that we do not invalidate the member
      }

      if (req.authenticated() || !this->_auth->isActive()) {
        events::Authenticated(req, authMethod);
        return rest::ResponseCode::OK;
      } else if (this->_auth->isActive()) {
        events::CredentialsBad(req, authMethod);
        return rest::ResponseCode::UNAUTHORIZED;
      }

      // intentionally falls through
    } catch (arangodb::basics::Exception const& ex) {
      // translate error
      if (ex.code() == TRI_ERROR_USER_NOT_FOUND) {
        return rest::ResponseCode::UNAUTHORIZED;
      }
      return GeneralResponse::responseCode(ex.what());
    } catch (...) {
      return rest::ResponseCode::SERVER_ERROR;
    }
  }

  return rest::ResponseCode::UNAUTHORIZED;
}

/// decompress content
/*template <SocketType T>
bool Connection<T>::handleContentEncoding(HttpRequest& req) {
  // TODO consider doing the decoding on the fly
  auto encode = [&](std::string const& encoding) {
    uint8_t* src = req.body().data();
    size_t len = req.body().length();
    if (encoding == "gzip") {
      VPackBuffer<uint8_t> dst;
      if (!arangodb::encoding::gzipUncompress(src, len, dst)) {
        return false;
      }
      req.body() = std::move(dst);
      return true;
    } else if (encoding == "deflate") {
      VPackBuffer<uint8_t> dst;
      if (!arangodb::encoding::gzipDeflate(src, len, dst)) {
        return false;
      }
      req.body() = std::move(dst);
      return true;
    }
    return false;
  };

  bool found;
  std::string const& val1 = req.header(StaticStrings::TransferEncoding, found);
  if (found) {
    return encode(val1);
  }

  std::string const& val2 = req.header(StaticStrings::ContentEncoding, found);
  if (found) {
    return encode(val2);
  }
  return true;
}*/

template <SocketType T>
void Connection<T>::sendResponse(std::unique_ptr<Response> response) {

  // CORS response handling
  if (!_origin.empty()) {
    // the request contained an Origin header. We have to send back the
    // access-control-allow-origin header now
    std::cout << "handling CORS response";

    // send back original value of "Origin" header
    response.setHeaderNCIfNotSet("access-control-allow-origin", _origin);

    // send back "Access-Control-Allow-Credentials" header
    response.setHeaderNCIfNotSet("access-control-allow-credentials",
                                 (_denyCredentials ? "false" : "true"));

    // use "IfNotSet" here because we should not override HTTP headers set
    // by Foxx applications
    response.setHeaderNCIfNotSet("access-control-expose-headers",
                                 "etag, content-encoding, content-length, location, server");
  }

  std::unique_ptr<std::string> body = std::move(response.body);
  std::array<asio::const_buffer, 2> buffers;
  buffers[0] = asio::buffer(header->data(), header->size());
  if (HTTP_HEAD != _parser.method) {
    buffers[1] = asio::buffer(body->data(), body->size());
  }

  // FIXME measure performance w/o sync write
  auto cb = [self = CommTask::shared_from_this(),
             h = std::move(header),
             b = std::move(body)](asio::error_code ec,
                                  size_t nwrite) {
    auto* thisPtr = static_cast<Connection<T>*>(self.get());
    
    llhttp_errno_t err = llhttp_get_errno(&thisPtr->_parser);
    if (ec || !thisPtr->_shouldKeepAlive || err != HPE_PAUSED) {
      if (ec) {
        LOG_TOPIC("2b6b4", DEBUG, arangodb::Logger::REQUESTS)
        << "asio write error: '" << ec.message() << "'";
      }
      thisPtr->close();
    } else {  // ec == HPE_PAUSED
      llhttp_resume(&thisPtr->_parser);
      thisPtr->asyncReadSome();
    }
  };
  asio::async_write(this->_protocol->socket, std::move(buffers), std::move(cb));
}

template class asiodemo::rest::Connection<SocketType::Tcp>;
template class asiodemo::rest::Connection<SocketType::Ssl>;
#ifndef _WIN32
template class asiodemo::rest::Connection<SocketType::Unix>;
#endif
