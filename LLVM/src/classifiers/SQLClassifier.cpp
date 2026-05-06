#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectSQL(const std::string_view s) {
    const auto trimmed = TrimLeft(s);
    if (trimmed.size() < 6)
      return {Language::Unknown, 0.0f};

    static constexpr std::string_view leaders[] = {
      "select ", "insert ", "update ", "delete ", "create ",
      "alter ", "drop ", "merge ", "with ", "grant ",
      "revoke ", "begin ", "commit ", "rollback ", "explain ",
    };
    bool hasLeader = false;
    for (const auto leader: leaders) {
      if (StartsWithCI(trimmed, leader)) {
        hasLeader = true;
        break;
      }
    }
    if (!hasLeader)
      return {Language::Unknown, 0.0f};

    static constexpr std::string_view keywords[] = {
      " from ", " where ", " join ", " inner ", " outer ",
      " left ", " right ", " group ", " order ", " having ",
      " limit ", " values", " into ", " set ", " table ",
      " index ", " on ", " and ", " or ", " not ",
      " in ", " like ", " between ", " exists ", " null",
      " as ", " distinct", " union ",
    };
    int kwCount = 0;
    for (const auto kw: keywords) {
      if (FindCI(s, kw) != std::string_view::npos)
        kwCount++;
    }
    if (kwCount == 0)
      return {Language::Unknown, 0.0f};
    return {Language::SQL, std::min(0.65f + static_cast<float>(kwCount) * 0.05f, 0.95f)};
  }
} // namespace classifier_internal
