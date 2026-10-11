
#pragma once

#include <cstdint>
#include <string>

namespace incidents {

struct BreakdownPlan {
  bool occurs = false;

  // Fraccion del recorrido donde ocurre.
  // Ejemplo: 0.45 significa el 45% del viaje.
  double atFraction = 0.0;
};

namespace detail {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;

constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

inline void addByte(std::uint64_t &hash, unsigned char value) noexcept {
  hash ^= static_cast<std::uint64_t>(value);
  hash *= kFnvPrime;
}

inline void addNumber(std::uint64_t &hash, std::uint64_t value) noexcept {
  for (int i = 0; i < 8; ++i) {
    addByte(hash, static_cast<unsigned char>((value >> (i * 8)) & 0xFF));
  }
}

inline void addText(std::uint64_t &hash, const std::string &value) noexcept {
  addNumber(hash, static_cast<std::uint64_t>(value.size()));

  for (unsigned char character : value) {
    addByte(hash, character);
  }
}

inline std::uint64_t makeHash(unsigned int seed, const std::string &courierId,
                              const std::string &orderId,
                              const std::string &leg,
                              const std::string &purpose) noexcept {
  std::uint64_t hash = kFnvOffset;

  addNumber(hash, seed);
  addText(hash, courierId);
  addText(hash, orderId);
  addText(hash, leg);
  addText(hash, purpose);

  return hash;
}

inline double toUnitInterval(std::uint64_t hash) noexcept {
  // Utilizar 53 bits para generar
  // un valor uniforme en [0, 1).
  constexpr double denominator = 9007199254740992.0;

  return static_cast<double>(hash >> 11) / denominator;
}

} // namespace detail

// =====================================================
// PLANIFICAR UNA AVERIA PARA UN VIAJE
// =====================================================

inline BreakdownPlan makeBreakdownPlan(unsigned int seed,
                                       const std::string &courierId,
                                       const std::string &orderId,
                                       const std::string &leg,
                                       double breakdownProbability) noexcept {
  const double occurrenceSample = detail::toUnitInterval(
      detail::makeHash(seed, courierId, orderId, leg, "occurrence"));

  const double positionSample = detail::toUnitInterval(
      detail::makeHash(seed, courierId, orderId, leg, "position"));

  const bool occurs =
      breakdownProbability >= 1.0 ||
      (breakdownProbability > 0.0 && occurrenceSample < breakdownProbability);

  // Reservamos un margen al inicio y al final.
  // Si ocurre una averia, sera durante el viaje.
  const double atFraction = 0.10 + 0.80 * positionSample;

  return BreakdownPlan{occurs, atFraction};
}

} // namespace incidents
