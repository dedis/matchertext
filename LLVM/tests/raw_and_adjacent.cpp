// Raw string and adjacent literal fixture.

const auto kSql = R"(SELECT * FROM logs WHERE payload = "{[(raw)]}")";
const auto kJson =
    "{"
    "\"items\": ["
    "\"one\","
    "\"two\""
    "]"
    "}";

const auto kRegex = R"delim(^\[(.*)\]\((.*)\)$)delim";

int UseFixtures() {
  return kSql[0] + kJson[0] + kRegex[0];
}
