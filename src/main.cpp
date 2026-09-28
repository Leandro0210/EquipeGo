#include <iostream>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  std::cout << "OpenCV version: " << CV_VERSION << "\n";
  return 0;
}