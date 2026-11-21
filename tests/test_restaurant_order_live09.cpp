#include "analytics/RestaurantOrderLive09.hpp"
#include "orders/Order.hpp"

#include <cstddef>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define EXIGIR(x) do { if (!(x)) { \
  std::cerr << "FALLO: " #x " linea " << __LINE__ << '\n'; return 1; \
} } while(false)

struct RestaurantePrueba { std::string id; };

int main() {
  using equipego::analytics::agruparPendientesReales09;
  using equipego::analytics::indexarRestaurantes09;
  const std::vector<RestaurantePrueba> restaurantes{{"r0"}, {"r1"}, {"r2"}};
  const auto indice = indexarRestaurantes09(restaurantes);
  std::deque<Order> pendientes;
  pendientes.emplace_back("o0", "r1", "n0", 100);
  pendientes.emplace_back("o1", "r0", "n0", 101);
  pendientes.emplace_back("o2", "r1", "n0", 102);
  pendientes.emplace_back("o3", "r2", "n0", 103);
  pendientes.emplace_back("o4", "r0", "n0", 104);

  const auto vista = agruparPendientesReales09(pendientes, indice, restaurantes.size());
  EXIGIR((vista.agrupacion.starts == std::vector<std::size_t>{0, 2, 4, 5}));
  EXIGIR((vista.agrupacion.indices == std::vector<std::size_t>{1, 4, 0, 2, 3}));
  EXIGIR(vista.gruposNoVacios == 3);
  EXIGIR(vista.maximoGrupo == 2);
  // Los pedidos reales nunca cambian de orden ni de identidad.
  for (std::size_t i = 0; i < pendientes.size(); ++i) {
    EXIGIR(pendientes[i].orderId == "o" + std::to_string(i));
    EXIGIR(pendientes[i].time == static_cast<long long>(100 + i));
    EXIGIR(pendientes[i].getState() == OrderState::Created);
  }

  const std::deque<Order> vacios;
  auto vacio = agruparPendientesReales09(vacios, indice, restaurantes.size());
  EXIGIR((vacio.agrupacion.starts == std::vector<std::size_t>{0, 0, 0, 0}));
  EXIGIR(vacio.agrupacion.indices.empty());

  const std::vector<RestaurantePrueba> ninguno;
  EXIGIR(agruparPendientesReales09(vacios, indexarRestaurantes09(ninguno), 0)
             .agrupacion.starts.size() == 1);

  bool fallo = false;
  try {
    std::deque<Order> desconocido;
    desconocido.emplace_back("oX", "rDesconocido", "n0", 0);
    (void)agruparPendientesReales09(desconocido, indice, 3);
  } catch (const std::invalid_argument&) { fallo = true; }
  EXIGIR(fallo);

  fallo = false;
  try { (void)indexarRestaurantes09(
      std::vector<RestaurantePrueba>{{"r0"}, {"r0"}}); }
  catch (const std::invalid_argument&) { fallo = true; }
  EXIGIR(fallo);

  std::deque<Order> muchos;
  for (std::size_t i = 0; i < 1025; ++i) {
    muchos.emplace_back("x" + std::to_string(i), "r" + std::to_string(i % 3), "n0", 0);
  }
  const auto grandes = agruparPendientesReales09(muchos, indice, 3);
  EXIGIR(grandes.agrupacion.indices.size() == muchos.size());
  EXIGIR(grandes.agrupacion.starts.back() == muchos.size());
  for (std::size_t r = 0; r < 3; ++r) {
    for (auto i = grandes.agrupacion.starts[r]; i < grandes.agrupacion.starts[r + 1]; ++i) {
      const auto p = grandes.agrupacion.indices[i];
      EXIGIR(p % 3 == r);
      if (i > grandes.agrupacion.starts[r]) EXIGIR(grandes.agrupacion.indices[i - 1] < p);
    }
  }
  EXIGIR(muchos.front().orderId == "x0" && muchos.back().orderId == "x1024");
  std::cout << "TEST SCAN09 EN PEDIDOS REALES (VISTA FIFO): OK\n";
  return 0;
}
