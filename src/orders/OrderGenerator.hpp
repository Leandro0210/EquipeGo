#pragma once

#include "config/Config.hpp"

#include <cstddef>
#include <random>

class OrderGenerator {
private:
  OrdersConfig config;
  std::mt19937 rng;

public:
  explicit OrderGenerator(const OrdersConfig &config);

  int nextIntervalMs();

  int nextBurstSize();

  std::size_t nextRestaurantIndex(std::size_t restaurantCount);
  std::size_t nextDeliveryNodeIndex(std::size_t nodeCount);
};
