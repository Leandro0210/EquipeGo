#include "Config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <unordered_set>


namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr double kEarthRadiusMeters = 6371000.0;
constexpr double kPi = 3.14159265358979323846;

double degToRad(double deg) { return deg * (kPi / 180.0); }

// Calcula la distancia real en metros entre dos coordenadas (fórmula de
// Haversine)
double calculateDistanceMeters(double lat1, double lon1, double lat2,
                               double lon2) {
  const double dLat = degToRad(lat2 - lat1);
  const double dLon = degToRad(lon2 - lon1);
  const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                   std::cos(degToRad(lat1)) * std::cos(degToRad(lat2)) *
                       std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return kEarthRadiusMeters * c;
}

bool requireObject(const json &parent, const std::string &key,
                   const std::string &ctx) {
  if (!parent.contains(key) || !parent[key].is_object()) {
    std::cerr << "Invalid config: missing or non-object property '" << ctx
              << "." << key << "'.\n";
    return false;
  }
  return true;
}

bool requireString(const json &parent, const std::string &key,
                   const std::string &ctx) {
  if (!parent.contains(key) || !parent[key].is_string() ||
      parent[key].get<std::string>().empty()) {
    std::cerr << "Invalid config: missing or non-empty string property '" << ctx
              << "." << key << "'.\n";
    return false;
  }
  return true;
}

bool requireNumber(const json &parent, const std::string &key,
                   const std::string &ctx) {
  if (!parent.contains(key) || !parent[key].is_number()) {
    std::cerr << "Invalid config: missing or non-numeric property '" << ctx
              << "." << key << "'.\n";
    return false;
  }
  return true;
}

bool requireInteger(const json &parent, const std::string &key,
                    const std::string &ctx) {
  if (!parent.contains(key) || !parent[key].is_number_integer()) {
    std::cerr << "Invalid config: missing or non-integer property '" << ctx
              << "." << key << "'.\n";
    return false;
  }
  return true;
}

bool requireBool(const json &parent, const std::string &key,
                 const std::string &ctx) {
  if (!parent.contains(key) || !parent[key].is_boolean()) {
    std::cerr << "Invalid config: missing or non-boolean property '" << ctx
              << "." << key << "'.\n";
    return false;
  }
  return true;
}

bool requireArray(const json &parent, const std::string &key,
                  const std::string &ctx) {
  if (!parent.contains(key) || !parent[key].is_array()) {
    std::cerr << "Invalid config: missing or non-array property '" << ctx << "."
              << key << "'.\n";
    return false;
  }
  return true;
}

} // namespace

cv::Point latLonToPixel(double lat, double lon, const MapBounds &bounds,
                        int width, int height) {
  const double xRatio = (lon - bounds.west) / (bounds.east - bounds.west);
  const double yRatio = (bounds.north - lat) / (bounds.north - bounds.south);
  const int x =
      static_cast<int>(std::round(xRatio * static_cast<double>(width)));
  const int y =
      static_cast<int>(std::round(yRatio * static_cast<double>(height)));
  return cv::Point(x, y);
}

bool loadAndValidateConfig(int argc, char *argv[],
                           SimulationConfig &outConfig) {
  // 1. Validar argumentos de línea de comandos (Sección 5)
  if (argc < 2) {
    std::cerr << "Usage: " << (argc > 0 ? argv[0] : "delivery_sim")
              << " <config.json> [--log <path>]\n";
    return false;
  }

  const std::string configFilePath = argv[1];
  outConfig.logFilePath = "events.log";

  if (argc != 2 && argc != 4) {
    std::cerr << "Invalid arguments. Expected: <config.json> [--log <path>]\n";
    return false;
  }
  if (argc == 4) {
    if (std::string(argv[2]) != "--log" || std::string(argv[3]).empty()) {
      std::cerr << "Invalid flag. Expected: --log <path>\n";
      return false;
    }
    outConfig.logFilePath = argv[3];
  }

  // 2. Abrir y parsear el archivo JSON
  std::ifstream file(configFilePath);
  if (!file.is_open()) {
    std::cerr << "Error: cannot open configuration file '" << configFilePath
              << "'.\n";
    return false;
  }

  json root;
  try {
    file >> root;
  } catch (const json::parse_error &e) {
    std::cerr << "Error: malformed JSON in '" << configFilePath
              << "': " << e.what() << "\n";
    return false;
  }

  if (!root.is_object()) {
    std::cerr << "Invalid config: root JSON must be an object.\n";
    return false;
  }

  // 3. Validar sección "map"
  if (!requireObject(root, "map", "root"))
    return false;
  const json &jMap = root["map"];
  if (!requireString(jMap, "image", "map"))
    return false;
  if (!requireString(jMap, "attribution", "map"))
    return false;
  if (!requireObject(jMap, "bounds", "map"))
    return false;

  const json &jBounds = jMap["bounds"];
  if (!requireNumber(jBounds, "north", "map.bounds") ||
      !requireNumber(jBounds, "south", "map.bounds") ||
      !requireNumber(jBounds, "west", "map.bounds") ||
      !requireNumber(jBounds, "east", "map.bounds")) {
    return false;
  }

  outConfig.map.imagePath = jMap["image"].get<std::string>();
  outConfig.map.attribution = jMap["attribution"].get<std::string>();
  outConfig.map.bounds.north = jBounds["north"].get<double>();
  outConfig.map.bounds.south = jBounds["south"].get<double>();
  outConfig.map.bounds.west = jBounds["west"].get<double>();
  outConfig.map.bounds.east = jBounds["east"].get<double>();

  if (outConfig.map.bounds.north <= outConfig.map.bounds.south ||
      outConfig.map.bounds.east <= outConfig.map.bounds.west) {
    std::cerr << "Invalid config: map.bounds must satisfy north > south and "
                 "east > west.\n";
    return false;
  }

  // Resolver la ruta de la imagen relativa al archivo de configuración
  const fs::path cfgPath(configFilePath);
  const fs::path baseDir =
      cfgPath.has_parent_path() ? cfgPath.parent_path() : fs::current_path();
  const fs::path resolvedImgPath = baseDir / fs::path(outConfig.map.imagePath);
  outConfig.map.resolvedPath = resolvedImgPath.string();

  outConfig.map.image =
      cv::imread(outConfig.map.resolvedPath, cv::IMREAD_COLOR);
  if (outConfig.map.image.empty()) {
    std::cerr << "Invalid config: cannot read map image at '"
              << outConfig.map.resolvedPath << "'.\n";
    return false;
  }

  const int imgWidth = outConfig.map.image.cols;
  const int imgHeight = outConfig.map.image.rows;

  // 4. Validar sección "nodes" (al menos uno)
  if (!requireArray(root, "nodes", "root"))
    return false;
  const json &jNodes = root["nodes"];
  if (jNodes.empty()) {
    std::cerr
        << "Invalid config: 'nodes' array must contain at least one node.\n";
    return false;
  }

  outConfig.nodes.clear();
  outConfig.nodeIndexById.clear();
  for (std::size_t i = 0; i < jNodes.size(); ++i) {
    const json &jNode = jNodes[i];
    if (!jNode.is_object()) {
      std::cerr << "Invalid config: node at index " << i
                << " is not an object.\n";
      return false;
    }
    if (!requireString(jNode, "id", "nodes") ||
        !requireNumber(jNode, "lat", "nodes") ||
        !requireNumber(jNode, "lon", "nodes")) {
      return false;
    }

    Node n;
    n.id = jNode["id"].get<std::string>();
    n.lat = jNode["lat"].get<double>();
    n.lon = jNode["lon"].get<double>();

    if (outConfig.nodeIndexById.find(n.id) != outConfig.nodeIndexById.end()) {
      std::cerr << "Invalid config: duplicate node id '" << n.id << "'.\n";
      return false;
    }

    n.pixelPos =
        latLonToPixel(n.lat, n.lon, outConfig.map.bounds, imgWidth, imgHeight);
    outConfig.nodeIndexById[n.id] = outConfig.nodes.size();
    outConfig.nodes.push_back(n);
  }

  // 5. Validar sección "streets" y referencias a nodos existentes
  if (!requireArray(root, "streets", "root"))
    return false;
  const json &jStreets = root["streets"];
  if (jStreets.empty()) {
    std::cerr << "Invalid config: 'streets' array cannot be empty.\n";
    return false;
  }

  std::unordered_set<std::string> streetIds;
  outConfig.streets.clear();
  for (std::size_t i = 0; i < jStreets.size(); ++i) {
    const json &jStreet = jStreets[i];
    if (!jStreet.is_object()) {
      std::cerr << "Invalid config: street at index " << i
                << " is not an object.\n";
      return false;
    }
    if (!requireString(jStreet, "id", "streets") ||
        !requireString(jStreet, "from", "streets") ||
        !requireString(jStreet, "to", "streets") ||
        !requireBool(jStreet, "oneWay", "streets")) {
      return false;
    }

    Street s;
    s.id = jStreet["id"].get<std::string>();
    s.from = jStreet["from"].get<std::string>();
    s.to = jStreet["to"].get<std::string>();
    s.oneWay = jStreet["oneWay"].get<bool>();

    if (!streetIds.insert(s.id).second) {
      std::cerr << "Invalid config: duplicate street id '" << s.id << "'.\n";
      return false;
    }

    auto itFrom = outConfig.nodeIndexById.find(s.from);
    auto itTo = outConfig.nodeIndexById.find(s.to);
    if (itFrom == outConfig.nodeIndexById.end()) {
      std::cerr << "Invalid config: street '" << s.id
                << "' references non-existent node '" << s.from << "'.\n";
      return false;
    }
    if (itTo == outConfig.nodeIndexById.end()) {
      std::cerr << "Invalid config: street '" << s.id
                << "' references non-existent node '" << s.to << "'.\n";
      return false;
    }
    if (s.from == s.to) {
      std::cerr << "Invalid config: street '" << s.id
                << "' has identical 'from' and 'to' node.\n";
      return false;
    }

    const Node &nFrom = outConfig.nodes[itFrom->second];
    const Node &nTo = outConfig.nodes[itTo->second];
    s.lengthMeters =
        calculateDistanceMeters(nFrom.lat, nFrom.lon, nTo.lat, nTo.lon);
    outConfig.streets.push_back(s);
  }

  // 6. Validar sección "restaurants" y referencias a nodos existentes
  if (!requireArray(root, "restaurants", "root"))
    return false;
  const json &jRestaurants = root["restaurants"];
  if (jRestaurants.empty()) {
    std::cerr << "Invalid config: 'restaurants' array cannot be empty.\n";
    return false;
  }

  std::unordered_set<std::string> restaurantIds;
  outConfig.restaurants.clear();
  for (std::size_t i = 0; i < jRestaurants.size(); ++i) {
    const json &jRest = jRestaurants[i];
    if (!jRest.is_object()) {
      std::cerr << "Invalid config: restaurant at index " << i
                << " is not an object.\n";
      return false;
    }
    if (!requireString(jRest, "id", "restaurants") ||
        !requireString(jRest, "name", "restaurants") ||
        !requireString(jRest, "node", "restaurants") ||
        !requireInteger(jRest, "pickupSlots", "restaurants") ||
        !requireArray(jRest, "prepTimeMs", "restaurants")) {
      return false;
    }

    Restaurant r;
    r.id = jRest["id"].get<std::string>();
    r.name = jRest["name"].get<std::string>();
    r.nodeId = jRest["node"].get<std::string>();
    r.pickupSlots = jRest["pickupSlots"].get<int>();

    if (!restaurantIds.insert(r.id).second) {
      std::cerr << "Invalid config: duplicate restaurant id '" << r.id
                << "'.\n";
      return false;
    }
    if (outConfig.nodeIndexById.find(r.nodeId) ==
        outConfig.nodeIndexById.end()) {
      std::cerr << "Invalid config: restaurant '" << r.id
                << "' references non-existent node '" << r.nodeId << "'.\n";
      return false;
    }
    if (r.pickupSlots < 1) {
      std::cerr << "Invalid config: restaurant '" << r.id
                << "' must have pickupSlots >= 1.\n";
      return false;
    }

    const json &jPrep = jRest["prepTimeMs"];
    if (jPrep.size() != 2 || !jPrep[0].is_number_integer() ||
        !jPrep[1].is_number_integer()) {
      std::cerr << "Invalid config: restaurant '" << r.id
                << "' prepTimeMs must be an array of 2 integers.\n";
      return false;
    }
    r.prepTimeMinMs = jPrep[0].get<int>();
    r.prepTimeMaxMs = jPrep[1].get<int>();
    if (r.prepTimeMinMs < 0 || r.prepTimeMinMs > r.prepTimeMaxMs) {
      std::cerr << "Invalid config: restaurant '" << r.id
                << "' has invalid prepTimeMs range.\n";
      return false;
    }

    outConfig.restaurants.push_back(r);
  }

  // 7. Validar sección "fleet"
  if (!requireObject(root, "fleet", "root"))
    return false;
  const json &jFleet = root["fleet"];
  if (!requireInteger(jFleet, "couriers", "fleet") ||
      !requireInteger(jFleet, "bagCapacity", "fleet") ||
      !requireNumber(jFleet, "speedKmh", "fleet") ||
      !requireString(jFleet, "startNode", "fleet")) {
    return false;
  }

  outConfig.fleet.couriers = jFleet["couriers"].get<int>();
  outConfig.fleet.bagCapacity = jFleet["bagCapacity"].get<int>();
  outConfig.fleet.speedKmh = jFleet["speedKmh"].get<double>();
  outConfig.fleet.startNode = jFleet["startNode"].get<std::string>();

  if (outConfig.fleet.couriers < 1 || outConfig.fleet.bagCapacity < 1 ||
      outConfig.fleet.speedKmh <= 0.0) {
    std::cerr << "Invalid config: fleet parameters out of valid range.\n";
    return false;
  }
  if (outConfig.nodeIndexById.find(outConfig.fleet.startNode) ==
      outConfig.nodeIndexById.end()) {
    std::cerr << "Invalid config: fleet.startNode '"
              << outConfig.fleet.startNode << "' does not exist in nodes.\n";
    return false;
  }

  // 8. Validar sección "orders"
  if (!requireObject(root, "orders", "root"))
    return false;
  const json &jOrders = root["orders"];
  if (!requireInteger(jOrders, "meanIntervalMs", "orders") ||
      !requireInteger(jOrders, "burstMax", "orders") ||
      !requireInteger(jOrders, "maxPending", "orders") ||
      !requireInteger(jOrders, "seed", "orders")) {
    return false;
  }

  outConfig.orders.meanIntervalMs = jOrders["meanIntervalMs"].get<int>();
  outConfig.orders.burstMax = jOrders["burstMax"].get<int>();
  outConfig.orders.maxPending = jOrders["maxPending"].get<int>();
  outConfig.orders.seed = static_cast<unsigned int>(jOrders["seed"].get<int>());

  if (outConfig.orders.meanIntervalMs <= 0 || outConfig.orders.burstMax < 1 ||
      outConfig.orders.maxPending < 0) {
    std::cerr << "Invalid config: orders parameters out of valid range.\n";
    return false;
  }

  // 9. Validar sección "dispatch"
  if (!requireObject(root, "dispatch", "root"))
    return false;
  const json &jDispatch = root["dispatch"];
  if (!requireInteger(jDispatch, "quoteTimeoutMs", "dispatch") ||
      !requireInteger(jDispatch, "acceptTimeoutMs", "dispatch")) {
    return false;
  }

  outConfig.dispatch.quoteTimeoutMs = jDispatch["quoteTimeoutMs"].get<int>();
  outConfig.dispatch.acceptTimeoutMs = jDispatch["acceptTimeoutMs"].get<int>();
  if (outConfig.dispatch.quoteTimeoutMs <= 0 ||
      outConfig.dispatch.acceptTimeoutMs <= 0) {
    std::cerr << "Invalid config: dispatch timeouts must be > 0.\n";
    return false;
  }

  // 10. Validar sección "incidents"
  if (!requireObject(root, "incidents", "root"))
    return false;
  const json &jIncidents = root["incidents"];
  if (!requireNumber(jIncidents, "breakdownProbability", "incidents"))
    return false;

  outConfig.incidents.breakdownProbability =
      jIncidents["breakdownProbability"].get<double>();
  if (outConfig.incidents.breakdownProbability < 0.0 ||
      outConfig.incidents.breakdownProbability > 1.0) {
    std::cerr << "Invalid config: incidents.breakdownProbability must be "
                 "between 0 and 1.\n";
    return false;
  }

  // 11. Validar sección "simulation"
  if (!requireObject(root, "simulation", "root"))
    return false;
  const json &jSim = root["simulation"];
  if (!requireInteger(jSim, "durationS", "simulation") ||
      !requireNumber(jSim, "timeScale", "simulation")) {
    return false;
  }

  outConfig.simulation.durationS = jSim["durationS"].get<int>();
  outConfig.simulation.timeScale = jSim["timeScale"].get<double>();
  if (outConfig.simulation.durationS < 0 ||
      outConfig.simulation.timeScale <= 0.0) {
    std::cerr << "Invalid config: simulation parameters out of valid range.\n";
    return false;
  }

  return true;
}