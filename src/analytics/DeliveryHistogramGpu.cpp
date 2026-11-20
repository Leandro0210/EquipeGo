#include "analytics/DeliveryHistogramGpu.hpp"

#include <memory>
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
  auto tarea = std::make_shared<std::packaged_task<ResultadoHistogramaGpu()>>(
      [datos = std::move(duracionesMs), variante]() {
        return ejecutarHistogramaEnGpu(datos, variante);
      });
  auto futuro = tarea->get_future();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (detener_) throw std::runtime_error("Cola CUDA detenida");
    cola_.emplace([tarea] { (*tarea)(); });
  }
  condicion_.notify_one();
  return futuro;
}

std::future<ResultadoReduccionGpu> PropietarioGpuHistograma::enviarEta(
    std::vector<EtaCandidato> candidatos) {
  auto tarea = std::make_shared<std::packaged_task<ResultadoReduccionGpu()>>(
      [datos = std::move(candidatos)] {
        return ejecutarReduccionEtaEnGpu(datos);
      });
  auto futuro = tarea->get_future();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (detener_) throw std::runtime_error("Cola CUDA detenida");
    cola_.emplace([tarea] { (*tarea)(); });
  }
  condicion_.notify_one();
  return futuro;
}

std::future<ResultadoScanGpu09> PropietarioGpuHistograma::enviarScan09(
    std::vector<std::size_t> restauranteIds, std::size_t restaurantes) {
  auto tarea = std::make_shared<std::packaged_task<ResultadoScanGpu09()>>(
      [datos = std::move(restauranteIds), restaurantes] {
        return ejecutarAgrupacionScanGpu09(datos, restaurantes);
      });
  auto futuro = tarea->get_future();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (detener_) throw std::runtime_error("Cola CUDA detenida");
    cola_.emplace([tarea] { (*tarea)(); });
  }
  condicion_.notify_one();
  return futuro;
}

void PropietarioGpuHistograma::ejecutarCola() {
  while (true) {
    std::function<void()> tarea;
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
