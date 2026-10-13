#pragma once

// DP10.2: memoizacion concurrente de una distancia entre dos nodos.
// La PRIMERA llamada por par (origen, destino) calcula la distancia;
// las otras llamadas reutilizan un shared_future de manera segura.
//
// Por que promise/shared_future:
// - No se ejecuta Dijkstra bajo el mutex global de la cache.
// - Solo un hilo calcula cada par, incluso con 64 motos.
// - Las esperas por el MISMO par son bloqueantes, sin busy waiting.
// - Si falla el calculo, todos los interesados reciben la excepcion.
//
// La red de calles es inmutable mientras vive el Dispatcher, por lo que
// guardar las distancias durante toda su vida no cambia los resultados.

#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <utility>

class SharedDistanceCache {
private:
  using Key = std::pair<std::string, std::string>;
  mutable std::mutex mutex_;
  std::map<Key, std::shared_future<double>> distances_;

public:
  SharedDistanceCache() = default;
  SharedDistanceCache(const SharedDistanceCache &) = delete;
  SharedDistanceCache &operator=(const SharedDistanceCache &) = delete;

  template <typename Calculator>
  double get(const std::string &origin, const std::string &destination,
             Calculator &&calculate) {
    std::shared_future<double> future;
    std::promise<double> owner;
    bool isOwner = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const Key key{origin, destination};
      const auto found = distances_.find(key);
      if (found != distances_.end()) {
        future = found->second;
      } else {
        future = owner.get_future().share();
        distances_.emplace(key, future);
        isOwner = true;
      }
    }

    if (isOwner) {
      try {
        owner.set_value(calculate());
      } catch (...) {
        owner.set_exception(std::current_exception());
      }
    }

    return future.get();
  }
};
