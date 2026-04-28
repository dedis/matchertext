const auto kUrl = u8"https://example.com/path?q=1";
const auto kSql = u"SELECT name FROM users WHERE active = 1";
const auto kHtml = U"<div class=\"hero\">Hi</div>";
const auto kIdentifier = L"metrics/ui.startup/FirstContentfulPaint";
const auto kJson =
    "{"
    "\"name\": \"matchertext\", "
    "\"count\": 2"
    "}";

int UsePrefixedLiterals() {
  return kUrl[0] + kSql[0] + kHtml[0] + kIdentifier[0] + kJson[0];
}
