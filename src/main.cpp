
#include "src/Server.h"

using namespace asiodemo;

int main(){
   
  rest::Server server;

  server.addHandler("/", [](rest::Request) {
    rest::Response res;



    return res;
  });

  server.listenAndServe();
}
