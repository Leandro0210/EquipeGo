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

std::future<equipego::convolution05::GpuResult05>
PropietarioGpuHistograma::enviarHeatmap05(
    equipego::convolution05::DemandGrid05 grid,
    equipego::convolution05::GpuAlgorithm05 algorithm) {
  auto task = std::make_shared<std::packaged_task<equipego::convolution05::GpuResult05()>>(
      [grid = std::move(grid), algorithm] {
        return equipego::convolution05::gaussianGpu05(grid, algorithm);
      });
  auto result = task->get_future();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (detener_) throw std::runtime_error("Cola CUDA detenida");
    cola_.emplace([task] { (*task)(); });
  }
  condicion_.notify_one();
  return result;
}

std::future<equipego::stencil06::ResultadoGpu06>
PropietarioGpuHistograma::enviarCobertura06(
    equipego::stencil06::Campo06 campo, unsigned int barridos,
    equipego::stencil06::VarianteGpu06 variante) {
  auto tarea = std::make_shared<std::packaged_task<equipego::stencil06::ResultadoGpu06()>>(
      [datos = std::move(campo), barridos, variante] {
        return equipego::stencil06::evolucionarGpu(datos, barridos, variante);
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

std::future<ResultadoCargaGpu10> PropietarioGpuHistograma::enviarCarga10(
    std::vector<std::uint32_t> reservas) {
  auto tarea = std::make_shared<std::packaged_task<ResultadoCargaGpu10()>>(
      [datos = std::move(reservas)] {
        return ejecutarCargaCouriersGpu10(datos);
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

std::future<ResultadoDemandaGpu11> PropietarioGpuHistograma::enviarDemanda11(
    std::vector<PedidoDemanda11> pedidos,
    std::size_t restaurantes, std::size_t franjas) {
  auto tarea = std::make_shared<std::packaged_task<ResultadoDemandaGpu11()>>(
      [datos = std::move(pedidos), restaurantes, franjas] {
        return ejecutarHistogramaDemandaGpu11(datos, restaurantes, franjas);
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

std::future<ResultadoFiltroGpu12> PropietarioGpuHistograma::enviarFiltro12(
    std::vector<std::uint64_t> esperasMs, std::uint64_t umbralMs) {
  auto tarea = std::make_shared<std::packaged_task<ResultadoFiltroGpu12()>>(
      [datos = std::move(esperasMs), umbralMs] {
        return ejecutarFiltroEsperasGpu12(datos, umbralMs);
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
