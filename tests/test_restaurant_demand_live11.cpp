#include "analytics/RestaurantDemandLive11.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace equipego::analytics;
static void exigir11(bool ok, const char* m) {
  if (!ok) throw std::runtime_error(m);
}
int main() {
  try {
    const std::vector<std::string> ids = {"restA","restB","restC"};
    std::istringstream stream(
      "{\"t\":0,\"event\":\"orderCreated\",\"order\":\"o1\",\"restaurant\":\"restB\"}\n"
      "{\"t\":31000,\"event\":\"orderAssigned\",\"order\":\"o1\"}\n"
      "{\"t\":59999,\"event\":\"orderCreated\",\"order\":\"o2\",\"restaurant\":\"restA\"}\n"
      "{\"t\":60000,\"event\":\"orderCreated\",\"order\":\"o3\",\"restaurant\":\"restB\"}\n"
      "{\"t\":120123,\"event\":\"orderCreated\",\"order\":\"o4\",\"restaurant\":\"restB\"}\n"
      "{\"t\":120500,\"event\":\"simulationStopping\"}\n");
    const auto cap = capturarDemandaJSONL11(stream, ids);
    exigir11(cap.pedidos.size()==4 && cap.franjas==3, "captura eventos");
    const auto h = histogramaDemandaCPU11(cap.pedidos, ids.size(), cap.franjas);
    exigir11(h.totalPedidos==4 && h.porRestaurante[0]==1 &&
             h.porRestaurante[1]==3 && h.porRestaurante[2]==0, "totales");
    exigir11(h.en(1,0)==1 && h.en(1,1)==1 && h.en(1,2)==1, "franjas");
    exigir11(histogramasIguales11(h,
       histogramaDemandaCPUParalelo11(cap.pedidos, ids.size(), cap.franjas, 3)),
       "paralelismo CPU");
    std::ostringstream csv;
    escribirDemandaCSV11(csv,h,ids,60000);
    exigir11(csv.str().find("restB,2,120000,1") != std::string::npos, "CSV");
    auto rechazado = [&ids](const std::string& lines) {
      std::istringstream in(lines);
      try { (void)capturarDemandaJSONL11(in, ids); return false; }
      catch (const std::exception&) {return true;}
    };
    exigir11(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"unknown\",\"t\":0}\n"), "desconocido");
    exigir11(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"restA\",\"t\":-1}\n"), "negativo");
    exigir11(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"restA\",\"t\":0}\n{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"restA\",\"t\":1}\n"), "duplicado");
    exigir11(rechazado("{\"event\":\"orderCreated\",\"order\":\"x\",\"restaurant\":\"restA\",\"t\":1200000000}\n"), "limite");
    std::istringstream empty("");
    const auto vacio = capturarDemandaJSONL11(empty, ids);
    exigir11(vacio.pedidos.empty() && vacio.franjas == 1, "vacio");
    std::cout << "FEATURE11_EVENTOS_E4_TEST_OK,CASES=10,READ_ONLY=SI\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FEATURE11_EVENTOS_E4_TEST_ERROR:" << error.what() << '\n';
    return 1;
  }
}
