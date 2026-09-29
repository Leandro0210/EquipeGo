#include <iostream>
#include <string>

struct Order {
  std::string orderId;
  std::string restauranteId;
  std::string deliveryNodeId;
  long long time;

  Order(std::string id, std::string rest, std::string node, long long t) {
    orderId = id;
    restauranteId = rest;
    deliveryNodeId = node;
    time = t;
  }

  // Prohíbe crear una copia
  Order(const Order &other) = delete;
  // Prohíbe asignar una copia
  Order &operator=(const Order &other) = delete;

  // Permitir movimiento (Para transferir el pedido de la fila a la moto)
  Order(Order &&other) = default;
  Order &operator=(Order &&other) = default;
};

int main() { return 0; }