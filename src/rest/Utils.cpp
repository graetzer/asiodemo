#include "Utils.h"

namespace asiodemo {
namespace rest {

Request::Type llhttpToRequestType(llhttp_t* p) {
  switch (p->method) {
    case HTTP_DELETE:
      return Request::Type::DELETE_REQ;
    case HTTP_GET:
      return Request::Type::GET;
    case HTTP_HEAD:
      return Request::Type::HEAD;
    case HTTP_POST:
      return Request::Type::POST;
    case HTTP_PUT:
      return Request::Type::PUT;
    case HTTP_OPTIONS:
      return Request::Type::OPTIONS;
    case HTTP_PATCH:
      return Request::Type::PATCH;
    default:
      return Request::Type::ILLEGAL;
  }
}

void tolowerInPlace(std::string& str) {
  size_t len = str.length();

  if (len == 0) {
    return;
  }

  for (std::string::iterator i = str.begin(); i != str.end(); ++i) {
    *i = ::tolower(*i);
  }
}

std::string trim(std::string const& sourceStr, std::string const& trimStr) {
  size_t s = sourceStr.find_first_not_of(trimStr);
  size_t e = sourceStr.find_last_not_of(trimStr);

  if (s == std::string::npos) {
    return std::string();
  } else {
    return std::string(sourceStr, s, e - s + 1);
  }
}

}}