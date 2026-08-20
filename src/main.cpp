#include <iostream>

#include "config.h"
#include "httplib.h"
#include "routes.h"

int main() {
  httplib::Server svr;
  oj::RegisterRoutes(svr);

  std::cout << "OJ server listening on http://localhost:" << oj::kServerPort << std::endl;
  return svr.listen("0.0.0.0", oj::kServerPort);
}
