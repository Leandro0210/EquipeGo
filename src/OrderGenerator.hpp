#pragma once

#include "Config.hpp"

#include <cstddef>
#include <random>

class OrderGenerator {
private:
  OrdersConfig config;
  std::mt19937 rng;

public:
  explicit OrderGenerator(const OrdersConfig &config);

  // Tiempo simulado hasta la siguiente llegada.
  int nextIntervalMs();

  // Cantidad de pedidos creados simultáneamente.
  int nextBurstSize();

  // Selecciona restaurante y nodo de entrega.
  std::size_t nextRestaurantIndex(std::size_t restaurantCount);
  std::size_t nextDeliveryNodeIndex(std::size_t nodeCount);
};
