#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <opencv2/core.hpp>
#include <string>
#include <unordered_map>
#include <vector>


struct MapBounds {
  double north = 0.0;
  double south = 0.0;
  double west = 0.0;
  double east = 0.0;
};

struct MapConfig {
  std::string imagePath;    
  std::string resolvedPath; 
  std::string attribution;
  MapBounds bounds;
  cv::Mat image; 
};

struct Node {
  std::string id;
  double lat = 0.0;
  double lon = 0.0;
  cv::Point pixelPos{0, 0}; 
};

struct Street {
  std::string id;
  std::string from;
  std::string to;
  bool oneWay = false;
  double lengthMeters = 0.0; 
};

struct Restaurant {
  std::string id;
  std::string name;
  std::string nodeId;
  int pickupSlots = 1;
  int prepTimeMinMs = 0;
  int prepTimeMaxMs = 0;
};

struct FleetConfig {
  int couriers = 1;
  int bagCapacity = 1;
  double speedKmh = 30.0;
  std::string startNode;
};

struct OrdersConfig {
  int meanIntervalMs = 20000;
  int burstMax = 1;
  int maxPending = 50;
  unsigned int seed = 42;
};

struct DispatchConfig {
  int quoteTimeoutMs = 200;
  int acceptTimeoutMs = 600000;
};

struct IncidentsConfig {
  double breakdownProbability = 0.0;
};

struct SimulationParams {
  int durationS = 3600;
  double timeScale = 60.0;
};

struct SimulationConfig {
  MapConfig map;
  std::vector<Node> nodes;
  std::unordered_map<std::string, std::size_t> nodeIndexById;
  std::vector<Street> streets;
  std::vector<Restaurant> restaurants;
  FleetConfig fleet;
  OrdersConfig orders;
  DispatchConfig dispatch;
  IncidentsConfig incidents;
  SimulationParams simulation;
  std::string logFilePath = "events.log";
};

cv::Point latLonToPixel(double lat, double lon, const MapBounds &bounds,
                        int width, int height);

bool loadAndValidateConfig(int argc, char *argv[], SimulationConfig &outConfig);

#endif 