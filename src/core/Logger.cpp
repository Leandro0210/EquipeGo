
#include "core/Logger.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

// =====================================================
// CONSTRUCTOR
// =====================================================

EventLogger::EventLogger(const std::string &filename)
    : fileStream(filename, std::ios::out | std::ios::trunc) {

  if (!fileStream.is_open()) {
    throw std::runtime_error("No se pudo abrir el archivo de eventos: " +
                             filename);
  }
}

// =====================================================
// DP7: LINEA DE INICIO OBLIGATORIA
// =====================================================

void EventLogger::logStart() {

  // El texto debe coincidir exactamente
  // con la especificacion.
  std::cout << "Simulation has started...\n" << std::flush;
}

// =====================================================
// DP7: ESCRITURA CONCURRENTE DE EVENTOS JSONL
// =====================================================

void EventLogger::logEvent(nlohmann::json eventJson,
                           long long simulatedTimeMs) {

  // =========================================
  // 1. VALIDAR EL EVENTO
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

  // Algunos eventos, como courierBreakdown
  // o simulationStopping, no tienen pedido.
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
  // 2. PROTEGER TODA LA OPERACION
  // =========================================

  std::lock_guard<std::mutex> lock(logMutex);

  long long eventTime = simulatedTimeMs;

  // =========================================
  // 3. GARANTIZAR TIEMPO MONOTONO
  // =========================================

  if (hasOrder) {

    const auto previous = lastOrderTimeMs.find(orderId);

    if (previous != lastOrderTimeMs.end()) {
      eventTime = std::max(eventTime, previous->second);
    }
  }

  // =========================================
  // 4. COMPLETAR EL JSON
  // =========================================

  // El reloj que recibe el metodo es la
  // fuente oficial del campo t.
  eventJson["t"] = eventTime;

  // Serializar el objeto completo antes
  // de comenzar a escribir la linea.
  const std::string serialized = eventJson.dump();

  // =========================================
  // 5. ESCRIBIR UNA LINEA COMPLETA
  // =========================================

  fileStream << serialized << '\n';

  if (!fileStream) {
    throw std::runtime_error("Error escribiendo el registro de eventos");
  }

  // =========================================
  // 6. ACTUALIZAR EL ULTIMO TIEMPO
  // =========================================

  if (hasOrder) {
    lastOrderTimeMs[orderId] = eventTime;
  }
}
