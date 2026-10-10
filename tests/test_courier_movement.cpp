
#include "config/Config.hpp"
#include "core/Clock.hpp"
#include "fleet/Courier.hpp"
#include "map/RoadNetwork.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
    SimulationConfig config;

    if (!loadAndValidateConfig(argc, argv, config)) {
        return 1;
    }

    SimulationClock clock;
    clock.start(config.simulation.timeScale);

    RoadNetwork network(config);

    Courier c0("c0", config, network, clock);
    Courier c1("c1", config, network, clock);

    const std::string target = "n23";

    auto route = network.shortestRoute(
        config.fleet.startNode, target);

    if (!route) {
        std::cerr << "ERROR: ruta inexistente\n";
        return 1;
    }

    const double expected =
        route->distanceMeters /
        (config.fleet.speedKmh / 3.6) /
        config.simulation.timeScale;

    std::thread t0(&Courier::run, &c0);
    std::thread t1(&Courier::run, &c1);

    const auto start = std::chrono::steady_clock::now();

    const bool assigned0 = c0.assignDestination(target);
    const bool assigned1 = c1.assignDestination(target);

    const auto deadline =
        start + std::chrono::duration<double>(expected + 3.0);

    bool arrived = false;

    if (assigned0 && assigned1) {
        while (std::chrono::steady_clock::now() < deadline) {
            if (c0.getCurrentNode() == target &&
                c1.getCurrentNode() == target) {
                arrived = true;
                break;
            }

            // Polling permitido solamente en este test.
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    }

    const double elapsed =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();

    c0.requestStop();
    c1.requestStop();

    t0.join();
    t1.join();

    std::cout << "=== TEST MOVIMIENTO CONCURRENTE ===\n";
    std::cout << "Tiempo esperado: " << expected << " s\n";
    std::cout << "Tiempo medido: " << elapsed << " s\n";

    std::cout << "c0: " << c0.getCurrentNode() << '\n';
    std::cout << "c1: " << c1.getCurrentNode() << '\n';

    if (!arrived ||
        elapsed < expected * 0.8 ||
        elapsed > expected * 1.5 + 0.5) {
        std::cerr << "ERROR: regresion detectada\n";
        return 1;
    }

    std::cout << "MOVIMIENTO CONCURRENTE: OK\n";
    std::cout << "HILOS FINALIZADOS: OK\n";

    return 0;
}
