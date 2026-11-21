#include "analytics/OrderWaitLive12.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace equipego::analytics;
static void exigir(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
int main() {
  try {
    // Un registro en orden de creacion, no de asignacion.
    std::istringstream src(
       "{\"event\":\"orderCreated\",\"order\":\"o1\",\"restaurant\":\"r1\",\"t\":100}\n"
       "{\"event\":\"orderCreated\",\"order\":\"o2\",\"restaurant\":\"r2\",\"t\":200}\n"
       "{\"event\":\"orderAssigned\",\"order\":\"o2\",\"t\":220}\n"
       "{\"event\":\"orderCreated\",\"order\":\"o3\",\"restaurant\":\"r3\",\"t\":300}\n"
       "{\"event\":\"orderAssigned\",\"order\":\"o1\",\"t\":600}\n"
       "{\"event\":\"orderCreated\",\"order\":\"o4\",\"restaurant\":\"r4\",\"t\":350}\n"
       "{\"event\":\"orderRejected\",\"order\":\"o4\",\"t\":400}\n"
       "{\"event\":\"simulationStopping\",\"t\":1000}\n");
    const auto cap = capturarEsperasJSONL12(src);
    exigir(cap.pedidos.size() == 4 && cap.asignados == 2 &&
           cap.rechazados == 1 && cap.pendientes == 1, "conteos");
    exigir((cap.esperasMs == std::vector<std::uint64_t>{500,20,700,50}),
           "tiempos de espera");
    const auto r = filtrarEsperasCPU12(cap.esperasMs, 49);
    exigir((r.indices == std::vector<std::size_t>{0,2,3}), "estable y estricto");
    const auto r2 = filtrarEsperasCPUParalelo12(cap.esperasMs, 49, 3);
    exigir(filtrosIguales12(r, r2), "paralelo");
    std::ostringstream out;
    escribirFiltroCSV12(out, cap, r);
    exigir(out.str().find("0,\"o1\",\"r1\",asignado,100,600,500,49") != std::string::npos,
           "CSV asignado");
    exigir(out.str().find("2,\"o3\",\"r3\",pendiente,300,1000,700,49") != std::string::npos,
           "CSV pendiente");
    auto rechazado = [](const std::string& j) {
      std::istringstream in(j);
      try { (void)capturarEsperasJSONL12(in); return false; }
      catch (const std::exception&) { return true; }
    };
    exigir(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"r\",\"t\":1}\n"), "sin parada");
    exigir(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"r\",\"t\":1}\n{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"r\",\"t\":2}\n{\"event\":\"simulationStopping\",\"t\":3}\n"), "duplicado");
    exigir(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"r\",\"t\":-1}\n{\"event\":\"simulationStopping\",\"t\":5}\n"), "tiempo negativo");
    exigir(rechazado("{\"event\":\"orderAssigned\",\"order\":\"x\",\"t\":4}\n{\"event\":\"simulationStopping\",\"t\":8}\n"), "asignacion sin creacion");
    exigir(rechazado("{\"event\":\"simulationStopping\",\"t\":7}\n{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"r\",\"t\":2}\n"), "evento despues cierre");
    exigir(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"r\",\"t\":10}\n{\"event\":\"orderRejected\",\"order\":\"x\",\"t\":9}\n{\"event\":\"simulationStopping\",\"t\":10}\n"), "espera negativa");
    std::istringstream empty("{\"event\":\"simulationStopping\",\"t\":0}\n");
    auto cero = capturarEsperasJSONL12(empty);
    exigir(cero.pedidos.empty() && cero.esperasMs.empty(), "sin pedidos");
    std::cout << "FEATURE12_EVENTOS_E4_TEST_OK,CASES=12,ORDER_STABLE=SI,READ_ONLY=SI\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FEATURE12_EVENTOS_E4_TEST_ERROR:" << e.what() << '\n';
    return 1;
  }
}
