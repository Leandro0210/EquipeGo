#pragma once

#include <chrono>

class SimulationClock {
private:
  double timeScale = 1.0;
  std::chrono::steady_clock::time_point startTime =
      std::chrono::steady_clock::now();

public:
  // Inicia o reinicia el reloj de simulación.
  void start(double scale) {
    timeScale = scale;
    startTime = std::chrono::steady_clock::now();
  }

  // Devuelve los milisegundos transcurridos en tiempo simulado.
  long long getSimulatedTimeMs() const {
    const auto ahora = std::chrono::steady_clock::now();

    const auto tiempoReal =
        std::chrono::duration<double, std::milli>(ahora - startTime).count();

    return static_cast<long long>(tiempoReal * timeScale);
  }
};
