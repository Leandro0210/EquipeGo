#include <chrono>
#include <iostream>
#include <thread>

void rutinaRepartidor() {
  std::cout << "Repartidor: Esperando un pedido" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(3));
  std::cout << "Repartidor: Pedido Entregado" << std::endl;
}

int main() {
  std::cout << "Iniciando el dia en la oficina" << std::endl;

  std::thread repartidor1(rutinaRepartidor);

  repartidor1.join();

  std::cout << "Se cierra la oficina, Hasta Mañana" << std::endl;

  return 0;
}