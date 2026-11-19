#include "analytics/EtaGpuDispatch.hpp"

#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define EXIGIR(x) do { if (!(x)) { std::cerr << "FALLO: " #x << " en " << __LINE__ << '\n'; return 1; } } while(false)

struct QuotePrueba {
  std::size_t courierIndex;
  std::string courierId;
  double etaSimulatedSeconds;
  QuotePrueba(std::size_t index, double eta)
      : courierIndex(index), courierId("c" + std::to_string(index)),
        etaSimulatedSeconds(eta) {}
  QuotePrueba(std::size_t index, std::string id, double eta)
      : courierIndex(index), courierId(std::move(id)),
        etaSimulatedSeconds(eta) {}
};

int main() {
  using namespace equipego::analytics;
  using Reloj = std::chrono::steady_clock;
  const auto limite = [] {return Reloj::now() + std::chrono::seconds(2);};

  const EnviarReduccionGpu08 gpuCorrecta = [](std::vector<EtaCandidato> cs) {
    std::promise<ResultadoReduccionGpu> p;
    ResultadoReduccionGpu r;
    r.mejor = reducirMejorEtaCPU(cs);
    p.set_value(r);
    return p.get_future();
  };
  const EnviarReduccionGpu08 gpuIncorrecta = [](std::vector<EtaCandidato>) {
    std::promise<ResultadoReduccionGpu> p;
    ResultadoReduccionGpu r;
    r.mejor = EtaCandidato{999, 0.0, true};
    p.set_value(r);
    return p.get_future();
  };

  std::vector<QuotePrueba> quotes{{9, 8}, {7, 2}, {3, 2}, {2, 10}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuCorrecta, limite(), 1) ==
          EstadoEleccionGpu08::Usada);
  EXIGIR(quotes[0].courierIndex == 3);
  EXIGIR(quotes[1].courierIndex == 7);
  EXIGIR(quotes[2].courierIndex == 9);
  EXIGIR(quotes[3].courierIndex == 2);

  // Prueba obligatoria de (ETA, courierId): c10 gana a c2.
  quotes = {{2, "c2", 1.0}, {11, "c11", 1.0}, {10, "c10", 1.0}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuCorrecta, limite(), 1) ==
          EstadoEleccionGpu08::Usada);
  EXIGIR(quotes[0].courierId == "c10");
  EXIGIR(quotes[1].courierId == "c11");
  EXIGIR(quotes[2].courierId == "c2");
  quotes = {{2, "c2", 1.0}, {11, "c11", 1.0}, {10, "c10", 1.0}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuIncorrecta, limite(), 1) ==
          EstadoEleccionGpu08::DiferenciaCpuGpu);
  EXIGIR(quotes[0].courierId == "c10");

  quotes = {{9, 8}, {7, 2}, {3, 2}, {2, 10}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuIncorrecta, limite(), 1) ==
          EstadoEleccionGpu08::DiferenciaCpuGpu);
  EXIGIR(quotes[0].courierIndex == 3);

  quotes = {{2, 9}, {1, 4}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, {}, limite(), 1) ==
          EstadoEleccionGpu08::SoloCpu);
  EXIGIR(quotes[0].courierIndex == 1);

  quotes = {{2, 9}, {1, 4}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuCorrecta, limite(), 100) ==
          EstadoEleccionGpu08::SoloCpu);

  quotes = {{2, 9}, {1, 4}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuCorrecta,
          Reloj::now() - std::chrono::milliseconds(1), 1) ==
          EstadoEleccionGpu08::SinTiempo);
  EXIGIR(quotes[0].courierIndex == 1);

  const EnviarReduccionGpu08 gpuExcepcion = [](std::vector<EtaCandidato>)
      -> std::future<ResultadoReduccionGpu> {
    throw std::runtime_error("GPU unavailable");
  };
  quotes = {{2, 9}, {1, 4}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuExcepcion, limite(), 1) ==
          EstadoEleccionGpu08::FalloGpu);
  EXIGIR(quotes[0].courierIndex == 1);

  // Futuro real que permanece pendiente, sin bloquear el dispatcher.
  std::vector<std::shared_ptr<std::promise<ResultadoReduccionGpu>>> pendientes;
  const EnviarReduccionGpu08 gpuLenta = [&pendientes](std::vector<EtaCandidato>) {
    auto p = std::make_shared<std::promise<ResultadoReduccionGpu>>();
    auto f = p->get_future();
    pendientes.push_back(std::move(p));
    return f;
  };
  quotes = {{2, 9}, {1, 4}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuLenta,
          Reloj::now() + std::chrono::milliseconds(2), 1) ==
          EstadoEleccionGpu08::SinTiempo);
  EXIGIR(quotes[0].courierIndex == 1);

  quotes = {{2, std::numeric_limits<double>::quiet_NaN()},
            {3, -1}, {8, 9}, {1, 9}};
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuCorrecta, limite(), 1) ==
          EstadoEleccionGpu08::Usada);
  EXIGIR(quotes.size() == 2 && quotes[0].courierIndex == 1);

  quotes.clear();
  EXIGIR(ordenarConGpu08Ocpu(quotes, gpuCorrecta, limite(), 1) ==
          EstadoEleccionGpu08::SoloCpu);
  EXIGIR(quotes.empty());

  std::cout << "TEST REDUCTION DESPACHO GPU/CPU 08: OK\n";
  return 0;
}
