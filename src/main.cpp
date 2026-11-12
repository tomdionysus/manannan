#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "manannan/application.h"
#include "manannan/loggers/logger_stdio.h"

int main(int argc, char** argv) {
  return manannan::run_application(std::vector<std::string>(argv + 1, argv + argc),
                                   std::make_shared<manannan::loggers::LoggerStdIO>(manannan::loggers::INFO), std::cout,
                                   std::cerr);
}
