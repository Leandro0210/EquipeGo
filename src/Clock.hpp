#ifndef CLOCK_HPP
#define CLOCK_HPP

#include <chrono>

class SimulationClock {
private:
  double timeScale = 1.0;
  std::chrono::time_point<std::chrono::steady_clock> startTime;

public:
  // Inicia el cronómetro guardando la escala y el tiempo real en el que arrancó
  void start(double scale) {
    timeScale = scale;
    startTime = std::chrono::steady_clock::now();
  }

  // Calcula el tiempo simulado desde que se llamó a start()
  long long getSimulatedTimeMs() const {
    auto ahora = std::chrono::steady_clock::now();
    auto msReales =
        std::chrono::duration_cast<std::chrono::milliseconds>(ahora - startTime)
            .count();

    // Multiplicamos los milisegundos reales por la escala de la simulación
    return static_cast<long long>(msReales * timeScale);
  }
};

#endif // CLOCK_HPP