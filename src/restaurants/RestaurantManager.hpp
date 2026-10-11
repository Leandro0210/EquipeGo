
#pragma once

#include "config/Config.hpp"
#include "restaurants/PickupGate.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Un solo administrador compartido por toda la flota.
// Cada restaurante tiene su propio PickupGate.
class RestaurantManager {
private:
  std::unordered_map<std::string, std::shared_ptr<PickupGate>> gates;

public:
  explicit RestaurantManager(const std::vector<Restaurant> &restaurants) {
    for (const Restaurant &restaurant : restaurants) {
      auto gate = std::make_shared<PickupGate>(
          static_cast<std::size_t>(restaurant.pickupSlots));

      const bool inserted = gates.emplace(restaurant.id, gate).second;

      if (!inserted) {
        throw std::invalid_argument("Restaurante duplicado: " + restaurant.id);
      }
    }
  }

  RestaurantManager(const RestaurantManager &) = delete;
  RestaurantManager &operator=(const RestaurantManager &) = delete;

  // Devuelve el MISMO gate para todas las motos
  // que soliciten un mismo restaurante.
  std::shared_ptr<PickupGate> gateFor(const std::string &restaurantId) const {
    const auto it = gates.find(restaurantId);

    if (it == gates.end()) {
      throw std::out_of_range("Restaurante inexistente: " + restaurantId);
    }

    return it->second;
  }

  std::size_t size() const noexcept { return gates.size(); }

  // Despertar a todos los repartidores en fila.
  void requestStop() {
    for (const auto &entry : gates) {
      entry.second->requestStop();
    }
  }
};
