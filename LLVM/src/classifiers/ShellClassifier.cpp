#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectShell(const std::string_view s) {
    if (s.starts_with("#!/"))
      return {Language::Shell, 0.95f};

    int signals = 0;

    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '|') {
        const bool doublePipe =
            (i > 0 && s[i - 1] == '|') || (i + 1 < s.size() && s[i + 1] == '|');
        if (!doublePipe)
          signals += 2;
      }
    }

    if (s.find(">>") != std::string_view::npos)
      signals++;
    if (s.find("2>&1") != std::string_view::npos)
      signals += 2;
    if (s.find("2>/dev/null") != std::string_view::npos)
      signals += 2;

    const auto trimmed = TrimLeft(s);
    static constexpr std::string_view cmds[] = {
      "echo ", "cat ", "grep ", "sed ", "awk ", "find ",
      "xargs ", "ls ", "cd ", "mv ", "cp ", "rm ",
      "mkdir ", "chmod ", "chown ", "tar ", "curl ", "wget ",
      "ssh ", "scp ", "git ", "docker ", "make ", "cmake ",
      "pip ", "npm ", "apt ", "yum ", "brew ", "sudo ",
    };
    for (const auto cmd: cmds) {
      if (trimmed.starts_with(cmd)) {
        signals += 2;
        break;
      }
    }

    for (size_t i = 0; i + 1 < s.size(); i++) {
      if (s[i] == '$' &&
          (std::isalpha(static_cast<unsigned char>(s[i + 1])) ||
           s[i + 1] == '{' || s[i + 1] == '('))
        signals++;
    }

    if (signals < 2)
      return {Language::Unknown, 0.0f};
    return {Language::Shell, std::min(0.55f + static_cast<float>(signals) * 0.08f, 0.95f)};
  }
} // namespace classifier_internal
