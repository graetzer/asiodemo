
#include "rest/Server.h"

using namespace asiodemo;

int main(){
   
  rest::Server server;

  server.addHandler("/", [](rest::Request) {
    auto res = std::make_unique<rest::Response>();
    res->status_code = rest::ResponseCode::OK;
    res->body = std::make_unique<std::string>();
    res->body->append("Hello World");
    return res;
  });
  
  server.addHandler("/abcd", [](rest::Request) {
    auto res = std::make_unique<rest::Response>();
    res->status_code = rest::ResponseCode::OK;
    res->body = std::make_unique<std::string>();
    res->body->append("Hello World");
    return res;
  });

  server.listenAndServe();
}
