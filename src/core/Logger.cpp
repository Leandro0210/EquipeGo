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
// DP7: MENSAJE INICIAL
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

  // Toda la escritura queda protegida.
  std::lock_guard<std::mutex> lock(logMutex);

  long long eventTime = simulatedTimeMs;

  if (hasOrder) {
    const auto previous = lastOrderTimeMs.find(orderId);

    if (previous != lastOrderTimeMs.end()) {
      eventTime = std::max(eventTime, previous->second);
    }
  }

  eventJson["t"] = eventTime;

  // El JSON se serializa antes de escribirlo.
  const std::string serialized = eventJson.dump();

  // 1. Registro persistente.
  fileStream << serialized << '\n';

  if (!fileStream) {
    throw std::runtime_error("Error escribiendo el registro de eventos");
  }

  // 2. Registro observable por stdout.
  // Se escribe dentro del mismo mutex.
  if (emitToStdout) {
    std::cout << serialized << '\n' << std::flush;

    if (!std::cout) {
      throw std::runtime_error("Error escribiendo eventos en stdout");
    }
  }

  if (hasOrder) {
    lastOrderTimeMs[orderId] = eventTime;
  }
}

// =====================================================
// DP7: FINALIZAR ESCRITURA DEL REGISTRO
// =====================================================

void EventLogger::flush() {
  std::lock_guard<std::mutex> lock(logMutex);

  fileStream.flush();

  if (!fileStream) {
    throw std::runtime_error("No se pudo completar la escritura del JSONL");
  }
}