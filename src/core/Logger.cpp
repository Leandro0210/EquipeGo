#include "core/Logger.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

EventLogger::EventLogger(const std::string &filename, bool emitToStdout)
    : fileStream(filename, std::ios::out | std::ios::trunc),
      emitToStdout(emitToStdout) {
  if (!fileStream.is_open()) {
    throw std::runtime_error("No se pudo abrir el archivo de eventos: " +
                             filename);
  }
}

void EventLogger::logStart() {
  std::lock_guard<std::mutex> lock(logMutex);
  std::cout << "Simulation has started...\n" << std::flush;
}

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

  const std::string name = eventIt->get<std::string>();
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

  std::lock_guard<std::mutex> lock(logMutex);

  long long eventTime = simulatedTimeMs;
  if (hasOrder) {
    const auto previous = lastOrderTimeMs.find(orderId);
    if (previous != lastOrderTimeMs.end()) {
      eventTime = std::max(eventTime, previous->second);
    }
  }

  eventJson["t"] = eventTime;
  const std::string serialized = eventJson.dump();

  fileStream << serialized << '\n';
  if (emitToStdout) {
    std::cout << serialized << '\n' << std::flush;
  }

  if (hasOrder) {
    lastOrderTimeMs[orderId] = eventTime;
  }

  if (name == "orderCreated") {
    ++liveStats.created;
  } else if (name == "orderDelivered") {
    ++liveStats.delivered;
  } else if (name == "orderRejected") {
    ++liveStats.rejected;
  } else if (name == "courierBreakdown") {
    ++liveStats.breakdowns;
  } else if (name == "orderReassigned") {
    ++liveStats.reassigned;
  } else if (name == "courierBreakStarted") {

    ++liveStats.breaks;
  }
}

LiveEventStats EventLogger::snapshotStats() const {
  std::lock_guard<std::mutex> lock(logMutex);
  return liveStats;
}

void EventLogger::flush() {
  std::lock_guard<std::mutex> lock(logMutex);
  fileStream.flush();
  if (!fileStream) {
    throw std::runtime_error("No se pudo completar la escritura del JSONL");
  }
  if (emitToStdout) {
    std::cout.flush();
    if (!std::cout) {
      throw std::runtime_error("No se pudo completar la escritura en stdout");
    }
  }
}
