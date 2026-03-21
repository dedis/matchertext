//
// AtomicString.hpp
// Author: Antoine Bastide
// Date: 09.03.2026
//

#ifndef ATOMIC_STRING_HPP
#define ATOMIC_STRING_HPP
#include <atomic>
#include <memory>
#include <string>

/// Thread-safe immutable string holder.
/// Uses atomic operations on shared_ptr to allow lock-free reads.
class AtomicString {
  public:

    /// Replace the stored string atomically.
    void set(std::string s) {
      const auto ptr = std::make_shared<const std::string>(std::move(s));
      std::atomic_store(&data, ptr);
    }

    /// Get the current string snapshot.
    [[nodiscard]] std::string get() const {
      const auto ptr = std::atomic_load(&data);
      return ptr ? *ptr : "";
    }

  private:
    std::shared_ptr<const std::string> data{};
};

#endif //ATOMIC_STRING_HPP
