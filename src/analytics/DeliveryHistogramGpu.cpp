#include "analytics/DeliveryHistogramGpu.hpp"

#include <stdexcept>
#include <utility>

namespace equipego::analytics {

PropietarioGpuHistograma::PropietarioGpuHistograma()
    : hilo_(&PropietarioGpuHistograma::ejecutarCola, this) {}

PropietarioGpuHistograma::~PropietarioGpuHistograma() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    detener_ = true;
  }
  condicion_.notify_all();
  if (hilo_.joinable()) hilo_.join();
}

std::future<ResultadoHistogramaGpu> PropietarioGpuHistograma::enviar(
    std::vector<long long> duracionesMs, VarianteHistogramaGpu variante) {
  std::packaged_task<ResultadoHistogramaGpu()> tarea(
      [datos = std::move(duracionesMs), variante]() {
        return ejecutarHistogramaEnGpu(datos, variante);
      });
  auto futuro = tarea.get_future();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (detener_) throw std::runtime_error("Cola CUDA detenida");
    cola_.push(std::move(tarea));
  }
  condicion_.notify_one();
  return futuro;
}

void PropietarioGpuHistograma::ejecutarCola() {
  while (true) {
    std::packaged_task<ResultadoHistogramaGpu()> tarea;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      condicion_.wait(lock, [this] { return detener_ || !cola_.empty(); });
      if (detener_ && cola_.empty()) return;
      tarea = std::move(cola_.front());
      cola_.pop();
    }
    // packaged_task propaga excepciones al futuro sin terminar el hilo.
    tarea();
  }
}

}  // namespace equipego::analytics
