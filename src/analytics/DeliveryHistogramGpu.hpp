#pragma once

#include "analytics/DeliveryHistogram.hpp"
#include "analytics/EtaReductionGpu.hpp"
#include "analytics/RestaurantOrderScanGpu.hpp"

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace equipego::analytics {

// Dos variantes del MISMO histograma: no cuentan como dos features.
enum class VarianteHistogramaGpu { AtomicosGlobales, CompartidaPrivada };

struct ResultadoHistogramaGpu {
  HistogramaEntregas bins{};
  double kernelMs = 0.0;
  double totalMs = 0.0;
};

// Solo el hilo propietario debe llamar al backend CUDA.
// Esta funcion se define en DeliveryHistogramCuda.cu.
ResultadoHistogramaGpu ejecutarHistogramaEnGpu(
    const std::vector<long long>& duracionesMs,
    VarianteHistogramaGpu variante);

// Cola de solicitudes + futuros: otros hilos NO tocan el contexto CUDA.
class PropietarioGpuHistograma {
 public:
  PropietarioGpuHistograma();
  ~PropietarioGpuHistograma();
  PropietarioGpuHistograma(const PropietarioGpuHistograma&) = delete;
  PropietarioGpuHistograma& operator=(const PropietarioGpuHistograma&) = delete;

  std::future<ResultadoHistogramaGpu> enviar(
      std::vector<long long> duracionesMs,
      VarianteHistogramaGpu variante = VarianteHistogramaGpu::CompartidaPrivada);

  // El mismo hilo propietario atiende Histogram 07 y Reduction 08.
  std::future<ResultadoReduccionGpu> enviarEta(
      std::vector<EtaCandidato> candidatos);

  // Feature 09: mismo hilo propietario para Scan de pedidos por restaurante.
  std::future<ResultadoScanGpu09> enviarScan09(
      std::vector<std::size_t> restauranteIds, std::size_t restaurantes);

 private:
  void ejecutarCola();

  std::mutex mutex_;
  std::condition_variable condicion_;
  std::queue<std::function<void()>> cola_;
  bool detener_ = false;
  std::thread hilo_;
};

}  // namespace equipego::analytics
