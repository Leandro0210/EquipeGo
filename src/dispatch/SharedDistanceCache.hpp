#pragma once


#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <utility>

class SharedDistanceCache {
private:
  using Key = std::pair<std::string, std::string>;
  mutable std::mutex mutex_;
  std::map<Key, std::shared_future<double>> distances_;

public:
  SharedDistanceCache() = default;
  SharedDistanceCache(const SharedDistanceCache &) = delete;
  SharedDistanceCache &operator=(const SharedDistanceCache &) = delete;

  template <typename Calculator>
  double get(const std::string &origin, const std::string &destination,
             Calculator &&calculate) {
    std::shared_future<double> future;
    std::promise<double> owner;
    bool isOwner = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const Key key{origin, destination};
      const auto found = distances_.find(key);
      if (found != distances_.end()) {
        future = found->second;
      } else {
        future = owner.get_future().share();
        distances_.emplace(key, future);
        isOwner = true;
      }
    }

    if (isOwner) {
      try {
        owner.set_value(calculate());
      } catch (...) {
        owner.set_exception(std::current_exception());
      }
    }

    return future.get();
  }
};
