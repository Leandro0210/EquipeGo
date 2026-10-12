#include "core/Logger.hpp"

#include <cstdio>
#include <iostream>
#include <string>

int main() {
  const std::string file = "dp85_live_event_stats_test.jsonl";
  try {
    {
      EventLogger logger(file, false);
      logger.logEvent({{"event", "orderCreated"}, {"order", "o1"},
                       {"restaurant", "r1"}}, 10);
      logger.logEvent({{"event", "orderCreated"}, {"order", "o2"},
                       {"restaurant", "r2"}}, 11);
      logger.logEvent({{"event", "orderDelivered"}, {"order", "o1"},
                       {"courier", "c1"}}, 20);
      logger.logEvent({{"event", "courierBreakdown"}, {"courier", "c0"}}, 21);
      logger.logEvent({{"event", "orderReassigned"}, {"order", "o2"},
                       {"from", "c0"}, {"to", "c1"}}, 22);
      const LiveEventStats snapshot = logger.snapshotStats();
      if (snapshot.created != 2 || snapshot.delivered != 1 ||
          snapshot.rejected != 0 || snapshot.inProgress() != 1 ||
          snapshot.breakdowns != 1 || snapshot.reassigned != 1) {
        std::cerr << "DP8.5: contadores incorrectos\n";
        return 1;
      }
      logger.flush();
    }
    std::remove(file.c_str());
    std::cout << "DP8.5 LIVE EVENT STATS TEST: OK\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "DP8.5: " << error.what() << '\n';
    return 1;
  }
}
