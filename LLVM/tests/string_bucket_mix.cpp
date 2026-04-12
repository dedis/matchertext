const auto kPath = "../config/settings.yaml";
const auto kFormat = "status=%s code=%d";
const auto kShell = R"(grep -R "needle" src | sed 's/foo/bar/' > out.txt)";
const auto kYaml = R"(service: matchertext
retries: 3
paths:
  - src
  - include)";
const auto kHex = "4d5a90000300000004000000ffff0000";
const auto kBinary = "\x89PNG\x0d\x0a\x1a\x0a\x00\x00\x00\x0dIHDR";
const auto kPlain = "This subsystem schedules jobs and retries transient failures automatically.";

int UseStringBucketMix() {
  return kPath[0] + kFormat[0] + kShell[0] + kYaml[0] + kHex[0] + kBinary[0] +
         kPlain[0];
}
