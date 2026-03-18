//
// StatsGenerator.hpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#ifndef STATS_GENERATOR_HPP
#define STATS_GENERATOR_HPP

#include <atomic>
#include <string>
#include <tuple>
#include <vector>

// "Private" macros
#define _declare_field(name, label, ...) double name{};
#define _declare_field_atomic(name, label, ...) std::atomic<double> name{0};
#define _load_field(name, label, ...) snapshot.name = this->name.load();
#define _push_field(name, label, desc) cols.emplace_back(label, name, desc);

#define CREATE_STATS_HPP(Name, DECLARATION, EXTRA_FIELDS)              \
  struct Name##Snapshot {                                              \
    DECLARATION(_declare_field)                                        \
    [[nodiscard]] std::vector<std::tuple<std::string, double, std::string>> ToColumns() const; \
  };                                                                   \
                                                                       \
  struct Name {                                                        \
    DECLARATION(_declare_field_atomic)                                 \
    EXTRA_FIELDS                                                       \
    void DeriveStats();                                                \
    Name##Snapshot Snapshot() const;                                   \
  };
#define CREATE_STATS_HPP_NO_EXTRA(Name, DECLARATION) CREATE_STATS_HPP(Name, DECLARATION, /**/)

#define CREATE_STATS_CPP(Name, DECLARATION)                            \
  Name##Snapshot Name::Snapshot() {                                    \
    DeriveStats();                                                     \
    Name##Snapshot snapshot{};                                         \
    DECLARATION(_load_field)                                           \
    return snapshot;                                                   \
  }                                                                    \
                                                                       \
  std::vector<std::tuple<std::string, double, std::string>>            \
  Name##Snapshot::ToColumns() const {                                  \
    std::vector<std::tuple<std::string, double, std::string>> cols;    \
    DECLARATION(_push_field)                                           \
    return cols;                                                       \
  }

#endif // STATS_GENERATOR_HPP