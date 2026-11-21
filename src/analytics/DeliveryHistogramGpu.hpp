#pragma once

#include "analytics/DeliveryHistogram.hpp"
#include "analytics/EtaReductionGpu.hpp"
#include "analytics/RestaurantOrderScanGpu.hpp"
#include "analytics/OrderHeatmapGpu05.hpp"
#include "analytics/CourierCoverageGpu06.hpp"
#include "analytics/CourierLoadReductionGpu10.hpp"

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

  // E4: Feature05 comparte la cola y el unico hilo propietario CUDA.
  std::future<equipego::convolution05::GpuResult05> enviarHeatmap05(
      equipego::convolution05::DemandGrid05 grid,
      equipego::convolution05::GpuAlgorithm05 algorithm);

  // E4 Stencil06: futuro enviado a la cola existente, un unico contexto GPU.
  std::future<equipego::stencil06::ResultadoGpu06> enviarCobertura06(
      equipego::stencil06::Campo06 campo, unsigned int barridos,
      equipego::stencil06::VarianteGpu06 variante);

  // Feature10: Reduction de carga; mismo hilo CUDA que 04/07/08/09.
  std::future<ResultadoCargaGpu10> enviarCarga10(
      std::vector<std::uint32_t> reservas);

 private:
  void ejecutarCola();

  std::mutex mutex_;
  std::condition_variable condicion_;
  std::queue<std::function<void()>> cola_;
  bool detener_ = false;
  std::thread hilo_;
};

}  // namespace equipego::analytics
