
#include "orders/OrderBook.hpp"

#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

int main() {

  OrderBook book(3);

  for (int i = 0; i < 3; ++i) {

    const std::string id = "test_order_" + std::to_string(i);

    Order order(id, "r3", "n23", 0);

    if (!book.tryAdd(std::move(order))) {
      std::cerr << "ERROR al insertar pedido\n";
      return 1;
    }
  }

  if (book.size() != 3) {
    std::cerr << "ERROR en el tamaño de la cola\n";
    return 1;
  }

  // Simular el final de la ejecucion.
  book.requestStop();

  // El consumidor ya no puede extraer pedidos.
  if (book.waitAndTake().has_value()) {
    std::cerr << "ERROR: consumidor activo despues del stop\n";
    return 1;
  }

  std::vector<Order> remaining = book.takeRemaining();

  std::set<std::string> ids;

  bool allPending = true;

  for (Order &order : remaining) {

    ids.insert(order.orderId);

    if (!order.transitionTo(OrderState::Pending)) {
      allPending = false;
    }

    if (order.getState() != OrderState::Pending) {
      allPending = false;
    }
  }

  const bool correctCount = remaining.size() == 3;

  const bool noDuplicates = ids.size() == 3;

  const bool emptyBook = book.size() == 0;

  std::cout << "=== TEST ORDERBOOK PENDING ===\n";

  std::cout << "Pedidos recuperados: " << remaining.size() << '\n';

  std::cout << "Sin duplicados: " << (noDuplicates ? "SI" : "NO") << '\n';

  std::cout << "Todos en Pending: " << (allPending ? "SI" : "NO") << '\n';

  std::cout << "OrderBook vacio: " << (emptyBook ? "SI" : "NO") << '\n';

  if (!correctCount || !noDuplicates || !allPending || !emptyBook) {

    std::cerr << "ORDERBOOK PENDING TEST: ERROR\n";
    return 1;
  }

  std::cout << "ORDERBOOK PENDING TEST: OK\n";

  return 0;
}
