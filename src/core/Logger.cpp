
#include "core/Logger.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

// =====================================================
// CONSTRUCTOR
// =====================================================

EventLogger::EventLogger(const std::string &filename, bool emitToStdout)
    : fileStream(filename, std::ios::out | std::ios::trunc),
      emitToStdout(emitToStdout) {

  if (!fileStream.is_open()) {
    throw std::runtime_error("No se pudo abrir el archivo de eventos: " +
                             filename);
  }
}

// =====================================================
// DP7: MENSAJE DE INICIO
// =====================================================

void EventLogger::logStart() {
  std::lock_guard<std::mutex> lock(logMutex);

  std::cout << "Simulation has started...\n" << std::flush;
}

// =====================================================
// DP7: REGISTRO CONCURRENTE JSONL
// =====================================================

void EventLogger::logEvent(nlohmann::json eventJson,
                           long long simulatedTimeMs) {

  // =========================================
  // VALIDAR EVENTO
  // =========================================

  if (!eventJson.is_object()) {
    throw std::invalid_argument("El evento debe ser un objeto JSON");
  }

  const auto eventIt = eventJson.find("event");

  if (eventIt == eventJson.end() || !eventIt->is_string() ||
      eventIt->get<std::string>().empty()) {
    throw std::invalid_argument("El evento necesita un nombre valido");
  }

  if (simulatedTimeMs < 0) {
    throw std::invalid_argument("El tiempo simulado no puede ser negativo");
  }

  std::string orderId;
  bool hasOrder = false;

  const auto orderIt = eventJson.find("order");

  if (orderIt != eventJson.end()) {

    if (!orderIt->is_string()) {
      throw std::invalid_argument("El ID del pedido debe ser una cadena");
    }

    orderId = orderIt->get<std::string>();

    if (orderId.empty()) {
      throw std::invalid_argument("El ID del pedido no puede estar vacio");
    }

    hasOrder = true;
  }

  // =========================================
  // BLOQUEO DEL LOGGER COMPARTIDO
  // =========================================

  std::lock_guard<std::mutex> lock(logMutex);

  long long eventTime = simulatedTimeMs;

  if (hasOrder) {

    const auto previous = lastOrderTimeMs.find(orderId);

    if (previous != lastOrderTimeMs.end()) {
      eventTime = std::max(eventTime, previous->second);
    }
  }

  eventJson["t"] = eventTime;

  // Serializar antes de escribir.
  const std::string serialized = eventJson.dump();

  // =========================================
  // ESCRIBIR EN ARCHIVO
  // =========================================

  // Los errores de I/O de std::ofstream se
  // conservan en el estado del stream.
  //
  // No lanzamos una excepcion aqui por I/O:
  // este metodo es llamado desde hilos de
  // repartidores, productor y dispatcher.
  //
  // Un fallo de disco no debe terminar el
  // proceso mediante std::terminate().
  fileStream << serialized << '\n';

  // =========================================
  // EMITIR TAMBIEN A STDOUT
  // =========================================

  if (emitToStdout) {

    // Conservamos los eventos observables
    // mientras el stream de salida funcione.
    std::cout << serialized << '\n' << std::flush;
  }

  // =========================================
  // ACTUALIZAR ULTIMA MARCA DE TIEMPO
  // =========================================

  if (hasOrder) {
    lastOrderTimeMs[orderId] = eventTime;
  }
}

// =====================================================
// DP7: VALIDAR ESCRITURAS AL FINALIZAR
// =====================================================

void EventLogger::flush() {

  std::lock_guard<std::mutex> lock(logMutex);

  // Aqui ya no deben existir hilos
  // escribiendo eventos.
  fileStream.flush();

  // El error puede haberse producido
  // durante una escritura anterior o
  // precisamente durante este flush.
  if (!fileStream) {
    throw std::runtime_error("No se pudo completar la escritura del JSONL");
  }

  // Detectar tambien errores acumulados
  // durante la emision de eventos a stdout.
  if (emitToStdout) {

    std::cout.flush();

    if (!std::cout) {
      throw std::runtime_error("No se pudo completar la escritura en stdout");
    }
  }
}
