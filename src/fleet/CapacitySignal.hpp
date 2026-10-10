
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

// Notifica cuando cambia la capacidad disponible
// en alguno de los repartidores.
class CapacitySignal {
private:
  mutable std::mutex signalMutex;
  std::condition_variable signalCv;

  std::uint64_t generation = 0;

public:
  // Obtener la version actual de la señal.
  std::uint64_t version() const {
    std::lock_guard<std::mutex> lock(signalMutex);
    return generation;
  }

  // Avisar que se produjo un cambio.
  void notifyChange() {
    {
      std::lock_guard<std::mutex> lock(signalMutex);
      ++generation;
    }

    signalCv.notify_all();
  }

  // Esperar una nueva notificacion, un timeout
  // o una solicitud de apagado.
  //
  // Devuelve true solamente si hubo un cambio
  // y no se solicito detener el sistema.
  bool waitUntilChange(std::uint64_t previousVersion,
                       std::chrono::steady_clock::time_point deadline,
                       const std::atomic<bool> &stopRequested) {
    std::unique_lock<std::mutex> lock(signalMutex);

    signalCv.wait_until(
        lock, deadline, [this, previousVersion, &stopRequested] {
          return generation != previousVersion || stopRequested.load();
        });

    return generation != previousVersion && !stopRequested.load();
  }
};
