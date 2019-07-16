
#ifndef RESPONSE_H
#define RESPONSE_H 1

#include <map>
#include <string>
#include <vector>

namespace asiodemo {
namespace rest {

enum class ResponseCode {
  CONTINUE = 100,
  SWITCHING_PROTOCOLS = 101,
  PROCESSING = 102,

  OK = 200,
  CREATED = 201,
  ACCEPTED = 202,
  PARTIAL = 203,
  NO_CONTENT = 204,
  RESET_CONTENT = 205,
  PARTIAL_CONTENT = 206,

  MOVED_PERMANENTLY = 301,
  FOUND = 302,
  SEE_OTHER = 303,
  NOT_MODIFIED = 304,
  TEMPORARY_REDIRECT = 307,
  PERMANENT_REDIRECT = 308,

  BAD = 400,
  UNAUTHORIZED = 401,
  PAYMENT_REQUIRED = 402,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  METHOD_NOT_ALLOWED = 405,
  NOT_ACCEPTABLE = 406,
  REQUEST_TIMEOUT = 408,
  CONFLICT = 409,
  GONE = 410,
  LENGTH_REQUIRED = 411,
  PRECONDITION_FAILED = 412,
  REQUEST_ENTITY_TOO_LARGE = 413,
  REQUEST_URI_TOO_LONG = 414,
  UNSUPPORTED_MEDIA_TYPE = 415,
  REQUESTED_RANGE_NOT_SATISFIABLE = 416,
  EXPECTATION_FAILED = 417,
  I_AM_A_TEAPOT = 418,
  UNPROCESSABLE_ENTITY = 422,
  LOCKED = 423,
  PRECONDITION_REQUIRED = 428,
  TOO_MANY_REQUESTS = 429,
  REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
  UNAVAILABLE_FOR_LEGAL_REASONS = 451,

  SERVER_ERROR = 500,
  NOT_IMPLEMENTED = 501,
  BAD_GATEWAY = 502,
  SERVICE_UNAVAILABLE = 503,
  HTTP_VERSION_NOT_SUPPORTED = 505,
  BANDWIDTH_LIMIT_EXCEEDED = 509,
  NOT_EXTENDED = 510
};

struct Response {
  ResponseCode status_code;
  std::map<std::string, std::string> headers;
  std::vector<std::string> cookies;
  std::unique_ptr<std::string> body;

  void setHeaderNCIfNotSet(std::string const& key, std::string const& value) {
    if (headers.find(key) != headers.end()) {
      // already set
      return;
    }
    headers.emplace(key, value);
  }

  std::string responseString() const;
  std::unique_ptr<std::string> generateHeader() const;
};
}
}

#endif
