#include "Request.h"

using namespace asiodemo;
using namespace asiodemo::rest;

void Request::parseUrl(const char* url, size_t urlLen) {
  this->fullUrl.reserve(urlLen);
  // get rid of '//'
  for (size_t i = 0; i < urlLen; ++i) {
    this->fullUrl.push_back(url[i]);
    if (url[i] == '/') {
      while (i + 1 < urlLen && url[i + 1] == '/') {
        ++i;
      }
    }
  }

  const char* start = this->fullUrl.data();
  const char* end = start + this->fullUrl.size();

  char const* q = start;
  while (q != end && *q != '?') {
    ++q;
  }

  if (q == end || *q == '?') {
    this->path.assign(start, q - start);
  }
  if (q == end) {
    return;
  }

  bool keyPhase = true;
  const char* keyBegin = ++q;
  const char* keyEnd = keyBegin;
  const char* valueBegin = nullptr;

  while (q != end) {
    if (keyPhase) {
      keyEnd = q;
      if (*q == '=') {
        keyPhase = false;
        valueBegin = q + 1;
      }
      ++q;
      continue;
    }

    if (q + 1 == end || *(q + 1) == '&') {
      ++q;  // skip ahead

      std::string val = std::string(valueBegin, q - valueBegin);
      params[std::string(keyBegin, keyEnd - keyBegin)] = std::move(val);

      keyPhase = true;
      keyBegin = q + 1;
      continue;
    }
    ++q;
  }
}