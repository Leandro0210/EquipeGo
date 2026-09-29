#include "Clock.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <chrono>
#include <iostream>
#include <thread> // Para simular una pausa


int main(int argc, char *argv[]) {
  SimulationConfig config;
  if (!loadAndValidateConfig(argc, argv, config)) {
    return 1;
  }

  std::cout << "\n--- INICIANDO PRUEBA DE RELOJ Y LOGGER ---\n";

  // 1. Encendemos el reloj (Acelerado a 60x como pide la simulación)
  SimulationClock reloj;
  reloj.start(60.0);

  // 2. Preparamos el Logger pasándole el nombre del archivo (events.log)
  EventLogger logger(config.logFilePath);
  logger.logStart(); // Esto imprime el mensaje obligatorio en consola

  // 3. Simulamos un evento inventado: Un pedido acaba de nacer
  nlohmann::json evento1 = {{"type", "orderCreated"}, {"orderId", "o1"}};
  // Lo mandamos a escribir. ¡Ojo! Aquí le inyectamos la hora del reloj.
  logger.logEvent(evento1, reloj.getSimulatedTimeMs());

  // 4. Hacemos que el programa "duerma" medio segundo real para que el tiempo
  // avance
  std::cout << "Esperando medio segundo real...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 5. Simulamos otro evento: El pedido se asignó a una moto
  nlohmann::json evento2 = {
      {"type", "orderAssigned"}, {"orderId", "o1"}, {"courier", "c1"}};
  // Lo mandamos a escribir. La hora aquí debería ser mayor.
  logger.logEvent(evento2, reloj.getSimulatedTimeMs());

  std::cout << "Prueba terminada. Se creó el archivo: " << config.logFilePath
            << "\n";
  return 0;
}