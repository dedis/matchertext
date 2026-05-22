//
// LanguageParser.cpp
// Author: Antoine Bastide
// Date: 29.04.2026
//

#include <algorithm>
#include <cctype>
#include <string>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <map>
#include <poll.h>
#include <ranges>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#include "../include/LanguageParser.hpp"
#include "../include/LanguageData.hpp"
#include "../include/Parser.hpp"
#include "../include/TreeSitterParser.hpp"

static std::string format(const std::string &tpl, const std::string &a, const std::string &b) {
  std::string out;
  out.reserve(tpl.size() + a.size() * 2 + b.size() * 2);

  int arg = 0;
  for (size_t i = 0; i < tpl.size(); ++i) {
    if (i + 1 < tpl.size() && tpl[i] == '{' && tpl[i + 1] == '}') {
      if (arg == 0)
        out += a;
      else if (arg == 1)
        out += b;
      else if (arg == 2 || arg == 3)
        out += b + ".out";
      ++arg;
      ++i;
    } else {
      out += tpl[i];
    }
  }
  return out;
}

static bool isAvailable(const std::string_view cmd) {
  const std::string check = "command -v " + std::string(cmd) + " >/dev/null 2>&1";
  return std::system(check.c_str()) == 0;
}

static std::string firstAvailable(const std::span<const std::string_view> candidates) {
  for (const auto c: candidates)
    if (isAvailable(c))
      return std::string(c);
  return "";
}

namespace {
  // Bidirectional persistent subprocess. One instance per thread per language.
  struct PersistentProcess {
    int write_fd = -1;
    int read_fd = -1;
    pid_t pid = -1;

    [[nodiscard]] bool valid() const {
      return write_fd >= 0 && read_fd >= 0 && pid > 0;
    }

    bool start(const std::string &cmd) {
      int to_child[2], from_child[2];
      if (pipe(to_child) < 0)
        return false;
      if (pipe(from_child) < 0) {
        ::close(to_child[0]);
        ::close(to_child[1]);
        return false;
      }

      posix_spawn_file_actions_t fa;
      posix_spawn_file_actions_init(&fa);
      posix_spawn_file_actions_adddup2(&fa, to_child[0], STDIN_FILENO);
      posix_spawn_file_actions_adddup2(&fa, from_child[1], STDOUT_FILENO);
      posix_spawn_file_actions_addclose(&fa, to_child[0]);
      posix_spawn_file_actions_addclose(&fa, to_child[1]);
      posix_spawn_file_actions_addclose(&fa, from_child[0]);
      posix_spawn_file_actions_addclose(&fa, from_child[1]);

      // Put the child in its own process group so kill(-pid, SIGKILL) reaches
      // both /bin/sh and the python3 it forks, preventing orphaned subprocesses.
      posix_spawnattr_t attr;
      posix_spawnattr_init(&attr);
      posix_spawnattr_setpgroup(&attr, 0);
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

      const char *argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};
      const int r = posix_spawn(&pid, "/bin/sh", &fa, &attr, const_cast<char **>(argv), environ);
      posix_spawn_file_actions_destroy(&fa);
      posix_spawnattr_destroy(&attr);

      ::close(to_child[0]);
      ::close(from_child[1]);

      if (r != 0) {
        ::close(to_child[1]);
        ::close(from_child[0]);
        pid = -1;
        return false;
      }

      write_fd = to_child[1];
      read_fd = from_child[0];
      return true;
    }

    // Send a file path, read back the JSON line. Returns false if the subprocess died or timed out.
    bool request(const std::string &path, std::string &out) const {
      const std::string msg = path + "\n";
      const char *p = msg.c_str();
      size_t remaining = msg.size();
      while (remaining > 0) {
        const ssize_t n = write(write_fd, p, remaining);
        if (n <= 0)
          return false;
        p += n;
        remaining -= static_cast<size_t>(n);
      }

      out.clear();
      char buf[4096];
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10000);

      while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
          std::cout << "File: " << path << " took to long to parse" << std::endl;
          return false;
        }

        const int wait_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
        );
        pollfd pfd = {read_fd, POLLIN, 0};
        if (poll(&pfd, 1, wait_ms) <= 0)
          return false;
        if (!(pfd.revents & POLLIN))
          return false;

        const ssize_t n = read(read_fd, buf, sizeof(buf));
        if (n <= 0)
          return false;
        out.append(buf, static_cast<size_t>(n));

        if (!out.empty() && out.back() == '\n') {
          out.pop_back();
          return true;
        }
      }
    }

    void stop() {
      if (write_fd >= 0) {
        ::close(write_fd);
        write_fd = -1;
      }
      if (read_fd >= 0) {
        ::close(read_fd);
        read_fd = -1;
      }
      if (pid > 0) {
        kill(-pid, SIGKILL); // kill entire process group (shell + python3 child)
        waitpid(pid, nullptr, 0);
        pid = -1;
      }
    }
  };

  struct ThreadProcesses {
    std::map<LanguageEnum, PersistentProcess> procs;

    ~ThreadProcesses() {
      for (auto &proc: procs | std::views::values)
        proc.stop();
    }
  };

  thread_local ThreadProcesses tl_procs;
}

bool LanguageParser::ExtractData(
  const LanguageEnum language, const std::string &filePath, Serde::JSON &result
) {
  // C/C++ use the in-process clang lexer; every other supported language uses an
  // in-process tree-sitter grammar (see src/TreeSitterParser.cpp).
  if (language == LanguageEnum::C || language == LanguageEnum::CPP)
    return Parser::ParseC_CPP(filePath, result) && result.IsArray();

  if (TreeSitter::IsTreeSitterLanguage(language))
    return TreeSitter::Parse(language, filePath, result) && result.IsArray();

  return false;
}

bool LanguageParser::ParseLanguage(const std::string &name, LanguageEnum &out) {
  std::string lower(name);
  std::ranges::transform(
    lower, lower.begin(), [](const unsigned char c) {
      return std::tolower(c);
    }
  );
  out = GetLanguage(lower);
  return out != LanguageEnum::Unknown;
}
