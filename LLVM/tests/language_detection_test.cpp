#include <iostream>
#include <string_view>
#include <vector>

#include "LanguageClassifier.hpp"

namespace {
struct TestCase {
  const char *name;
  std::string_view input;
  Language expected;
  float minConfidence = 0.5f;
};

int RunCase(const TestCase &test) {
  const ClassificationResult result = ClassifyString(test.input, test.minConfidence);
  if (result.language == test.expected)
    return 0;

  std::cerr << "language_detection_test failed: " << test.name << '\n'
            << "  expected: " << LanguageName(test.expected) << '\n'
            << "  actual:   " << LanguageName(result.language) << '\n'
            << "  confidence:" << result.confidence << '\n'
            << "  input:    " << test.input << '\n';
  return 1;
}
} // namespace

int main() {
  const std::vector<TestCase> tests = {
      {"url", "https://example.com/api/v1/items?id=42", Language::URL},
      {"data_url", "data:text/plain;base64,SGVsbG8=", Language::URL},
      {"file_path", "/usr/local/include/project/config.yaml", Language::FilePath},
      {"relative_file_path", "../src/LanguageClassifier.cpp", Language::FilePath},
      {"windows_file_path",
       R"(C:\Program Files\MatcherText\config\settings.json)",
       Language::FilePath},
      {"format_string", "user=%s count=%d", Language::FormatString},
      {"complex_format_string", "[%s] user=%s retries=%d code=%x",
       Language::FormatString},
      {"escaped_percent_format_string", "progress=%d%% owner=%s",
       Language::FormatString},
      {"sql", "SELECT id, name FROM users WHERE active = 1 ORDER BY name",
       Language::SQL},
      {"comment_wrapped_sql",
       "/* SELECT id, payload FROM logs WHERE level = 'warn' */",
       Language::SQL},
      {"cte_sql",
       "WITH recent AS (SELECT id, level FROM logs) SELECT * FROM recent WHERE level = 'warn'",
       Language::SQL},
      {"escaped_json", "{\\\"items\\\": [\\\"one\\\", \\\"two\\\"], \\\"count\\\": 2}",
       Language::JSON},
      {"json_array",
       "[{\"name\": \"one\"}, {\"name\": \"two\"}]",
       Language::JSON},
      {"multiline_escaped_json",
       "{\\n  \\\"meta\\\": {\\\"count\\\": 2},\\n  \\\"items\\\": [\\\"one\\\", \\\"two\\\"]\\n}",
       Language::JSON},
      {"yaml",
       "name: matchertext\nversion: 1\nitems:\n  - one\n  - two",
       Language::YAML},
      {"comment_wrapped_yaml",
       "/*\nservice: matchertext\nretries: 3\npaths:\n  - src\n*/",
       Language::YAML},
      {"yaml_document",
       "---\nservice: matchertext\nretries: 3\npaths:\n  - src\n  - include\n...",
       Language::YAML},
      {"html",
       "<!doctype html><html><body><div class=\"hero\">Hi</div></body></html>",
       Language::HTML},
      {"html_with_script_and_style",
       "<div><script>console.log('x')</script><style>.hero { color: red; }</style></div>",
       Language::HTML},
      {"xml",
       "<?xml version=\"1.0\"?><xsl:stylesheet version=\"1.0\"></xsl:stylesheet>",
       Language::XML},
      {"namespaced_xml",
       "<xsl:stylesheet version=\"1.0\"><xsl:template match=\"/\"/></xsl:stylesheet>",
       Language::XML},
      {"css", ".hero { color: red; margin: 0; padding: 4px; }", Language::CSS},
      {"regex", "^(?:foo|bar)\\d{2,4}$", Language::Regex},
      {"named_group_regex",
       "^(?P<name>[A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(?:true|false)$",
       Language::Regex},
      {"shell", "grep -R \"needle\" src | sed 's/foo/bar/' > out.txt",
       Language::Shell},
      {"shell_shebang", "#!/bin/sh\necho hello", Language::Shell},
      {"shell_with_vars",
       "find src -name '*.cpp' | xargs grep -n \"$TOKEN\" 2>/dev/null",
       Language::Shell},
      {"identifier_like", "ProcessBoundString::EncryptBuffer",
       Language::IdentifierLike},
      {"resource_identifier",
       "metrics/ui.startup/FirstContentfulPaint",
       Language::IdentifierLike},
      {"plain_text", "This function returns the current device state.",
       Language::PlainText},
      {"comment_wrapped_plain_text",
       "// This subsystem schedules jobs and retries transient failures automatically.",
       Language::PlainText},
      {"multiline_plain_text",
       "This subsystem schedules jobs.\nIt retries transient failures automatically.",
       Language::PlainText},
      {"hex_data", "4d5a90000300000004000000ffff0000", Language::HexData},
      {"grouped_hex_data",
       "4d5a9000:03000000:04000000:ffff0000",
       Language::HexData},
      {"binary_data", "\\x89PNG\\x0d\\x0a\\x1a\\x0a\\x00\\x00\\x00\\x0dIHDR",
       Language::BinaryData},
      {"octal_binary_data", "\\000\\377\\123\\045\\000\\001\\002\\003",
       Language::BinaryData},
      {"threshold_rejects_url", "https://example.com/api/v1/items?id=42",
       Language::Unknown, 0.99f},
      {"codeish_unknown", "foo::bar->baz", Language::Unknown},
      {"unknown_short", "ok", Language::Unknown},
  };

  int failures = 0;
  for (const auto &test : tests)
    failures += RunCase(test);

  if (failures != 0) {
    std::cerr << failures << " language detection test(s) failed.\n";
    return 1;
  }

  std::cout << "All language detection tests passed (" << tests.size() << " cases).\n";
  return 0;
}
