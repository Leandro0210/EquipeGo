#include "orders/OrderGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

OrderGenerator::OrderGenerator(const OrdersConfig &config)
    : config(config), rng(config.seed) {}

int OrderGenerator::nextIntervalMs() {
  std::exponential_distribution<double> distribution(
      1.0 / static_cast<double>(config.meanIntervalMs));

  const int interval = static_cast<int>(std::round(distribution(rng)));

  return std::max(1, interval);
}

int OrderGenerator::nextBurstSize() {
  std::uniform_int_distribution<int> distribution(1, config.burstMax);
  return distribution(rng);
}

std::size_t OrderGenerator::nextRestaurantIndex(std::size_t restaurantCount) {
  if (restaurantCount == 0) {
    throw std::invalid_argument("No hay restaurantes disponibles.");
  }

  std::uniform_int_distribution<std::size_t> distribution(0,
                                                          restaurantCount - 1);

  return distribution(rng);
}

std::size_t OrderGenerator::nextDeliveryNodeIndex(std::size_t nodeCount) {
  if (nodeCount == 0) {
    throw std::invalid_argument("No hay nodos disponibles.");
  }

  std::uniform_int_distribution<std::size_t> distribution(0, nodeCount - 1);

  return distribution(rng);
}