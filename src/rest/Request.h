
#ifndef REQUEST_H
#define REQUEST_H 1

#include <map>
#include <string>

namespace asiodemo {
namespace rest {

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

  void parseUrl(const* path, size_t len);

  public:

  Type method;
    std::string fullUrl; // path as specified
  std::string path; // path without query
  std::map<std::string, std::string> params;

  std::map<std::string, std::string> headers;
  std::string body;
};
}
}

#endif