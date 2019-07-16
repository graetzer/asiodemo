
#ifndef REQUEST_H
#define REQUEST_H 1

#include <map>
#include <string>

namespace asiodemo { namespace rest {

struct Request {
  enum class Type {
    DELETE_REQ = 0,  // windows redefines DELETE
    GET,
    POST,
    PUT,
    HEAD,
    PATCH,
    OPTIONS,
    ILLEGAL  // must be last
  };

  void parseUrl(const char* path, size_t len);

  std::string header(std::string const& key) const {
    bool found;
    return this->header(key, found);
  }
  std::string header(std::string const& key, bool& found) const {
    auto const& val = headers.find(key);
    if (val != headers.end()) {
      found = true;
      return val->second;
    }
    found = false;
    return std::string();
  }

  void setHeader(std::string key, std::string val) {
    headers.emplace(std::move(key), std::move(val));
  }

 public:
  Type method;
  std::string fullUrl;  // path as specified
  std::string path;     // path without query
  std::map<std::string, std::string> params;

  std::map<std::string, std::string> headers;
  std::string body;
};
}}  // namespace asiodemo::rest

#endif
