#include "analytics/CourierLayout04.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace equipego::performance04;

void exigir(bool ok, const char* mensaje) {
  if (!ok) throw std::runtime_error(mensaje);
}

void caso(std::size_t n) {
  std::vector<CourierAoS04> input;
  input.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    input.push_back(CourierAoS04{
        static_cast<float>(i % 199) * 11.5f,
        static_cast<float>(i % 137) * -17.25f,
        2.0f + static_cast<float>(i % 5),
        static_cast<std::int32_t>(i % 3),
        3,
        static_cast<std::int32_t>(i % 11 == 0 ? 2 : 0)});
  }
  const auto soa = convertirSoA(input);
  const auto referencia = calcularAoS(input, 900.0f, -1700.0f, 280.0f, 1, 3);
  const auto hilos = std::vector<unsigned int>{1, 2, 4, 8, 16};
  exigir(referencia.size() == n, "Tamanos incorrectos");
  for (unsigned int t : hilos) {
    const auto aos = calcularAoS(input, 900.0f, -1700.0f, 280.0f, t, 3);
    const auto sos = calcularSoA(soa, 900.0f, -1700.0f, 280.0f, t, 3);
    for (std::size_t i = 0; i < n; ++i) {
      exigir(aos[i] == referencia[i], "AoS paralelo no equivalente");
      exigir(sos[i] == referencia[i], "SoA no equivalente");
      if (input[i].state != 0 || input[i].reserved >= input[i].capacity) {
        exigir(sos[i] == -1.0f, "Repartidor no disponible incluido");
      } else {
        exigir(sos[i] >= 0.0f && std::isfinite(sos[i]), "ETA no finita");
      }
    }
  }
  std::cout << "LAYOUT04_CASE_OK,n=" << n << '\n';
}

int main() {
  try {
    for (std::size_t n : {0u, 1u, 2u, 17u, 30u, 31u, 64u, 257u, 512u,
                          2000u, 5000u}) caso(n);
    auto v = convertirSoA({CourierAoS04{}});
    v.state.clear();
    try {
      (void)calcularSoA(v, 0, 0, 100);
      throw std::runtime_error("No se detectaron columnas distintas");
    } catch (const std::invalid_argument&) {
    }
    try {
      (void)calcularAoS({}, 0, 0, 0, 0);
      throw std::runtime_error("No se detecto hilos=0");
    } catch (const std::invalid_argument&) {
    }
    std::cout << "LAYOUT04_CPU_EQUIVALENCIA_OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "LAYOUT04_CPU_ERROR:" << e.what() << '\n';
    return 1;
  }
}
