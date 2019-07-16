#include "Response.h"

using namespace asiodemo;
using namespace asiodemo::rest;


std::string Response::responseString(ResponseCode code) {
  switch (code) {
    //  Informational 1xx
    case ResponseCode::CONTINUE:
      return "100 Continue";
    case ResponseCode::SWITCHING_PROTOCOLS:
      return "101 Switching Protocols";
    case ResponseCode::PROCESSING:
      return "102 Processing";

    //  Success 2xx
    case ResponseCode::OK:
      return "200 OK";
    case ResponseCode::CREATED:
      return "201 Created";
    case ResponseCode::ACCEPTED:
      return "202 Accepted";
    case ResponseCode::PARTIAL:
      return "203 Non-Authoritative Information";
    case ResponseCode::NO_CONTENT:
      return "204 No Content";
    case ResponseCode::RESET_CONTENT:
      return "205 Reset Content";
    case ResponseCode::PARTIAL_CONTENT:
      return "206 Partial Content";

    //  Redirection 3xx
    case ResponseCode::MOVED_PERMANENTLY:
      return "301 Moved Permanently";
    case ResponseCode::FOUND:
      return "302 Found";
    case ResponseCode::SEE_OTHER:
      return "303 See Other";
    case ResponseCode::NOT_MODIFIED:
      return "304 Not Modified";
    case ResponseCode::TEMPORARY_REDIRECT:
      return "307 Temporary Redirect";
    case ResponseCode::PERMANENT_REDIRECT:
      return "308 Permanent Redirect";

    //  Client Error 4xx
    case ResponseCode::BAD:
      return "400 Bad Request";
    case ResponseCode::UNAUTHORIZED:
      return "401 Unauthorized";
    case ResponseCode::PAYMENT_REQUIRED:
      return "402 Payment Required";
    case ResponseCode::FORBIDDEN:
      return "403 Forbidden";
    case ResponseCode::NOT_FOUND:
      return "404 Not Found";
    case ResponseCode::METHOD_NOT_ALLOWED:
      return "405 Method Not Allowed";
    case ResponseCode::NOT_ACCEPTABLE:
      return "406 Not Acceptable";
    case ResponseCode::REQUEST_TIMEOUT:
      return "408 Request Timeout";
    case ResponseCode::CONFLICT:
      return "409 Conflict";
    case ResponseCode::GONE:
      return "410 Gone";
    case ResponseCode::LENGTH_REQUIRED:
      return "411 Length Required";
    case ResponseCode::PRECONDITION_FAILED:
      return "412 Precondition Failed";
    case ResponseCode::REQUEST_ENTITY_TOO_LARGE:
      return "413 Payload Too Large";
    case ResponseCode::REQUEST_URI_TOO_LONG:
      return "414 Request-URI Too Long";
    case ResponseCode::UNSUPPORTED_MEDIA_TYPE:
      return "415 Unsupported Media Type";
    case ResponseCode::REQUESTED_RANGE_NOT_SATISFIABLE:
      return "416 Requested Range Not Satisfiable";
    case ResponseCode::EXPECTATION_FAILED:
      return "417 Expectation Failed";
    case ResponseCode::I_AM_A_TEAPOT:
      return "418 I'm a teapot";
    case ResponseCode::UNPROCESSABLE_ENTITY:
      return "422 Unprocessable Entity";
    case ResponseCode::LOCKED:
      return "423 Locked";
    case ResponseCode::PRECONDITION_REQUIRED:
      return "428 Precondition Required";
    case ResponseCode::TOO_MANY_REQUESTS:
      return "429 Too Many Requests";
    case ResponseCode::REQUEST_HEADER_FIELDS_TOO_LARGE:
      return "431 Request Header Fields Too Large";
    case ResponseCode::UNAVAILABLE_FOR_LEGAL_REASONS:
      return "451 Unavailable For Legal Reasons";

    //  Server Error 5xx
    case ResponseCode::SERVER_ERROR:
      return "500 Internal Server Error";
    case ResponseCode::NOT_IMPLEMENTED:
      return "501 Not Implemented";
    case ResponseCode::BAD_GATEWAY:
      return "502 Bad Gateway";
    case ResponseCode::SERVICE_UNAVAILABLE:
      return "503 Service Unavailable";
    case ResponseCode::HTTP_VERSION_NOT_SUPPORTED:
      return "505 HTTP Version Not Supported";
    case ResponseCode::BANDWIDTH_LIMIT_EXCEEDED:
      return "509 Bandwidth Limit Exceeded";
    case ResponseCode::NOT_EXTENDED:
      return "510 Not Extended";

    // default
    default: {
      // print generic group responses, based on error code group
      int group = ((int)code) / 100;
      switch (group) {
        case 1:
          return std::to_string((int)code) + " Informational";
        case 2:
          return std::to_string((int)code) + " Success";
        case 3:
          return std::to_string((int)code) + " Redirection";
        case 4:
          return std::to_string((int)code) + " Client error";
        case 5:
          return std::to_string((int)code) + " Server error";
        default:
          break;
      }
    }
  }

  return std::to_string((int)code) + " Unknown";
}

std::unique_ptr<std::string> Response::generateHeader() const {
  // TODO lease buffers
  auto header = std::make_unique<std::string>();
  header->reserve(220);

  header->append("HTTP/1.1 ");
  header->append(Response::responseString(response.responseCode()));
  header->append("\r\n", 2);

  for (auto const& it : response.headers()) {
    std::string const& key = it.first;
    size_t const keyLength = key.size();
    // ignore content-length
    if (key == "content-length" ||
        key == "connection" ||
        key == "transfer-encoding") {
      continue;
    }

    // reserve enough space for header name + ": " + value + "\r\n"
    header->reserve(key.size() + 2 + it.second.size() + 2);

    char const* p = key.data();
    char const* end = p + keyLength;
    int capState = 1;
    while (p < end) {
      if (capState == 1) {
        // upper case
        header->push_back(::toupper(*p));
        capState = 0;
      } else if (capState == 0) {
        // normal case
        header->push_back(::tolower(*p));
        if (*p == '-') {
          capState = 1;
        } else if (*p == ':') {
          capState = 2;
        }
      } else {
        // output as is
        header->push_back(*p);
      }
      ++p;
    }

    header->append(": ", 2);
    header->append(it.second);
    header->append("\r\n", 2);
  }
  
  // add "Content-Type" header
    header->append("Content-Type: text/plain; charset=utf-8\r\n");

  // Cookies
  for (auto const& it : response.cookies()) {
    header->append("Set-Cookie: ");
    header->append(it);
    header->append("\r\n", 2);
  }

  header->append("Content-Length: ");
  header->append(std::to_string(response.bodySize()));
  header->append("\r\n", 2);
      
  header->append("Connection: Keep-Alive\r\n");
  header->append("Keep-Alive: timeout=60");
  header->append("\r\n\r\n");

  return header;
}