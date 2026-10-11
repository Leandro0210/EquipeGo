
#pragma once

#include "config/Config.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

// Duracion determinista de preparacion.
// No comparte RNG entre hilos.
// El mismo pedido, restaurante y seed
// producen siempre la misma duracion.
inline long long preparationDurationMs(const Restaurant &restaurant,
                                       const std::string &orderId,
                                       unsigned int seed) {
  if (restaurant.prepTimeMinMs < 0 ||
      restaurant.prepTimeMaxMs < restaurant.prepTimeMinMs) {
    throw std::invalid_argument("Intervalo prepTimeMs invalido");
  }

  // Hash FNV-1a de 64 bits.
  std::uint64_t hash = 14695981039346656037ULL;

  const auto addByte = [&hash](std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };

  // Incorporar la semilla de forma estable.
  for (unsigned i = 0; i < 4; ++i) {
    addByte(static_cast<std::uint8_t>((seed >> (i * 8)) & 0xFFu));
  }

  // Incorporar el restaurante.
  for (unsigned char c : restaurant.id) {
    addByte(c);
  }

  addByte(0xFFu);

  // Incorporar el pedido.
  for (unsigned char c : orderId) {
    addByte(c);
  }

  const std::uint64_t minimum =
      static_cast<std::uint64_t>(restaurant.prepTimeMinMs);

  const std::uint64_t maximum =
      static_cast<std::uint64_t>(restaurant.prepTimeMaxMs);

  const std::uint64_t range = maximum - minimum + 1ULL;

  return static_cast<long long>(minimum + hash % range);
}
