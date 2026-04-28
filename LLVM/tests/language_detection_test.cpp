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
    {"pseudo_url_with_newline_wrappers", "\\nhttp://www.attotech.com\\n\\n", Language::PseudoURL},
    {"email", "thomas@kaiser-linux.li", Language::Email},
    {
      "pseudo_email_contact", "Thomas Kaiser <thomas@kaiser-linux.li>",
      Language::PseudoEmail
    },
    {
      "pseudo_email_label", "Author: samr7@cs.washington.edu",
      Language::PseudoEmail
    },
    {
      "wrapped_url_contact", "Jean-Francois Moine <http://moinejf.free.fr>",
      Language::PseudoURL
    },
    {
      "pseudo_url_mixed_with_binary_payload",
      "SelectedBuyerAndSellerReportId\\n"
      "https://example.org/\\n"
      "https://example.org/bid.js\\n"
      "https://ad3.com/\\n"
      "\\x01\\x00\\x00\\x00\\x08s\\x00\\x01\\nbsid\\n"
      "\\x01\\x00\\x00\\x00\\x07b\\x00\\x01\\nsid\\n"
      "\\x01\\x00\\x00\\x00\\x06b\\x00\\x01\\nid",
      Language::PseudoURL
    },
    {"data_prefix_format_not_url", "data: %8ph", Language::FormatString},
    {
      "data_prefix_plain_text_not_url", "data: complete",
      Language::PlainText
    },
    {"file_path", "/usr/local/include/project/config.yaml", Language::FilePath},
    {"relative_file_path", "../src/LanguageClassifier.cpp", Language::FilePath},
    {"repo_header_path", "base/uuid.h", Language::FilePath},
    {"repo_nested_header_path", "chrome/browser/ai/features.h", Language::FilePath},
    {
      "repo_generated_header_path",
      "mojo/public/cpp/bindings/tests/fixed_array_size_unittest.test-mojom-shared.h",
      Language::FilePath
    },
    {
      "windows_file_path",
      R"(C:\Program Files\MatcherText\config\settings.json)",
      Language::FilePath
    },
    {"format_string", "user=%s count=%d", Language::FormatString},
    {
      "complex_format_string", "[%s] user=%s retries=%d code=%x",
      Language::FormatString
    },
    {
      "escaped_percent_format_string", "progress=%d%% owner=%s",
      Language::FormatString
    },
    {
      "width_length_format_string", "  LD_LOCK                        = %16lx\\n",
      Language::FormatString
    },
    {
      "sql", "SELECT id, name FROM users WHERE active = 1 ORDER BY name",
      Language::SQL
    },
    {
      "comment_wrapped_sql",
      "/* SELECT id, payload FROM logs WHERE level = 'warn' */",
      Language::SQL
    },
    {
      "cte_sql",
      "WITH recent AS (SELECT id, level FROM logs) SELECT * FROM recent WHERE level = 'warn'",
      Language::SQL
    },
    {
      "sql_leader_without_secondary_keyword", "SELECT id",
      Language::Unknown
    },
    {
      "escaped_json", "{\\\"items\\\": [\\\"one\\\", \\\"two\\\"], \\\"count\\\": 2}",
      Language::JSON
    },
    {
      "json_array",
      "[{\"name\": \"one\"}, {\"name\": \"two\"}]",
      Language::JSON
    },
    {
      "multiline_escaped_json",
      "{\\n  \\\"meta\\\": {\\\"count\\\": 2},\\n  \\\"items\\\": [\\\"one\\\", \\\"two\\\"]\\n}",
      Language::JSON
    },
    {
      "single_quoted_json_like_object",
      "{'type': 'number'}",
      Language::JSON
    },
    {
      "yaml",
      "name: matchertext\nversion: 1\nitems:\n  - one\n  - two",
      Language::YAML
    },
    {
      "comment_wrapped_yaml",
      "/*\nservice: matchertext\nretries: 3\npaths:\n  - src\n*/",
      Language::YAML
    },
    {
      "yaml_document",
      "---\nservice: matchertext\nretries: 3\npaths:\n  - src\n  - include\n...",
      Language::YAML
    },
    {
      "yaml_false_positive_error_hints",
      "Error: Unable to find debugfs/tracefs\nHint: Was your kernel compiled with debugfs/tracefs support?\nHint: Is the debugfs/tracefs filesystem mounted?",
      Language::PlainText
    },
    {
      "yaml_false_positive_counter_dump",
      "over: 0\ncount: 0\nmin: 0\navg: 0\nmax: 0\n",
      Language::PlainText
    },
    {
      "yaml_false_positive_numeric_labels",
      "0: generic\n1: Trust 120 SpaceCam\n2: other Trust 120 SpaceCam\n",
      Language::PlainText
    },
    {
      "yaml_false_positive_assembly_labels",
      "2: ldbu  r2,0(r5)\n9: stb   r2,0(r3)\n",
      Language::Unknown
    },
    {
      "html",
      "<!doctype html><html><body><div class=\"hero\">Hi</div></body></html>",
      Language::HTML
    },
    {
      "html_with_script_and_style",
      "<div><script>console.log('x')</script><style>.hero { color: red; }</style></div>",
      Language::HTML
    },
    {
      "xml",
      "<?xml version=\"1.0\"?><xsl:stylesheet version=\"1.0\"></xsl:stylesheet>",
      Language::XML
    },
    {
      "namespaced_xml",
      "<xsl:stylesheet version=\"1.0\"><xsl:template match=\"/\"/></xsl:stylesheet>",
      Language::XML
    },
    {
      "namespaced_self_closing_xml",
      "<xi:include href=\"riscv-64bit-cpu.xml\"/>",
      Language::XML
    },
    {
      "xml_false_positive_url_in_angle_brackets",
      "Jean-Francois Moine <http://moinejf.free.fr>",
      Language::PseudoURL
    },
    {
      "xml_false_positive_rust_path",
      "<std::path::PathBuf>::new",
      Language::Unknown
    },
    {
      "xml_false_positive_format_placeholder",
      "<%s:%d>\\n",
      Language::FormatString
    },
    {
      "xml_false_positive_single_colon_placeholder",
      "<decode:run>",
      Language::Unknown
    },
    {"css", ".hero { color: red; margin: 0; padding: 4px; }", Language::CSS},
    {"regex", "^(?:foo|bar)\\d{2,4}$", Language::Regex},
    {
      "named_group_regex",
      "^(?P<name>[A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(?:true|false)$",
      Language::Regex
    },
    {
      "shell", "grep -R \"needle\" src | sed 's/foo/bar/' > out.txt",
      Language::Shell
    },
    {"shell_shebang", "#!/bin/sh\necho hello", Language::Shell},
    {
      "shell_with_vars",
      "find src -name '*.cpp' | xargs grep -n \"$TOKEN\" 2>/dev/null",
      Language::Shell
    },
    {
      "inline_asm_operands", "\t%0, %1, %3                          \\n",
      Language::InlineAsm
    },
    {
      "inline_asm_directives",
      " ,\\\"a\\\"; \\n.balign 4, 0;                            \\n.popsection;                             \\n",
      Language::InlineAsm
    },
    {
      "identifier_like", "ProcessBoundString::EncryptBuffer",
      Language::IdentifierLike
    },
    {
      "cpp_reference_declaration_fragment",
      "Phone   &number  ",
      Language::CPP
    },
    {
      "resource_identifier",
      "metrics/ui.startup/FirstContentfulPaint",
      Language::Unknown
    },
    {
      "slash_identifier_without_extension",
      "foo/bar/baz",
      Language::Unknown
    },
    {
      "plain_text", "This function returns the current device state.",
      Language::PlainText
    },
    {
      "comment_wrapped_plain_text",
      "// This subsystem schedules jobs and retries transient failures automatically.",
      Language::PlainText
    },
    {
      "multiline_plain_text",
      "This subsystem schedules jobs.\nIt retries transient failures automatically.",
      Language::PlainText
    },
    {
      "plain_text_label_configuration_write", "Configuration Write",
      Language::PlainText
    },
    {
      "plain_text_label_collecting_samples", "Collecting samples...",
      Language::PlainText
    },
    {
      "plain_text_label_system_sample_rate", "System Sample Rate",
      Language::PlainText
    },
    {
      "plain_text_label_namespace_node", " Namespace Node - ",
      Language::PlainText
    },
    {
      "plain_text_label_mic_type_select", "Mic Type Select",
      Language::PlainText
    },
    {
      "plain_text_label_configuring_gpio", "Configuring GPIO\\n",
      Language::PlainText
    },
    {
      "decorated_plain_text_preview_dump", "---- Preview Register dump ----",
      Language::PlainText
    },
    {
      "decorated_plain_text_slice_banner", "----------- slice ---------",
      Language::PlainText
    },
    {
      "decorated_plain_text_time_banner",
      "------------------------------------------\\n\\t\\tTime\\n------------------------------------------\\n",
      Language::PlainText
    },
    {
      "decorated_plain_text_skb_dump",
      "\\n************** SKB dump ****************\\n",
      Language::PlainText
    },
    {
      "decorated_plain_text_btcoex_banner",
      "[BTCoex], ****************************************************************\\n",
      Language::PlainText
    },
    {
      "decorated_plain_text_end_banner",
      "***********************END***********************\\n",
      Language::PlainText
    },
    {
      "decorated_plain_text_q40_reset",
      "*******************************************\\nCalled q40_reset : press the RESET button!!\\n*******************************************\\n",
      Language::PlainText
    },
    {
      "decorated_plain_text_sync_banner", "******* SYNC *******",
      Language::PlainText
    },
    {
      "decorated_separator_capsule_unknown", "o-----------------------o\\n",
      Language::Unknown
    },
    {"hex_data", "4d5a90000300000004000000ffff0000", Language::HexData},
    {
      "grouped_hex_data",
      "4d5a9000:03000000:04000000:ffff0000",
      Language::HexData
    },
    {
      "binary_data", "\\x89PNG\\x0d\\x0a\\x1a\\x0a\\x00\\x00\\x00\\x0dIHDR",
      Language::PseudoBinaryData
    },
    {
      "pseudo_binary_data",
      "Selected buyer record\\n\\x01\\x00\\x00\\x00\\x08s\\x00\\x01\\nbsid\\nid",
      Language::PseudoBinaryData
    },
    {
      "pseudo_binary_data_text_prefix",
      "test_\\001\\002\\003\\n\\r",
      Language::PseudoBinaryData
    },
    {
      "pseudo_binary_data_ip_prefix",
      "1.2.3.4\\xF0\\x9F\\x92\\xA9",
      Language::PseudoBinaryData
    },
    {
      "pseudo_binary_data_html_wrapper",
      "<b>\\xF0\\x9F\\x8F\\xAB</b>",
      Language::PseudoBinaryData
    },
    {
      "octal_binary_data", "\\000\\377\\123\\045\\000\\001\\002\\003",
      Language::BinaryData
    },
    {
      "threshold_rejects_url", "https://example.com/api/v1/items?id=42",
      Language::Unknown, 0.99f
    },
    {
      "separator_stars_unknown", "**************************************\\n",
      Language::Unknown
    },
    {
      "separator_dashes_unknown", "--------------------------------------------\\n",
      Language::Unknown
    },
    {
      "separator_equals_unknown", "==========",
      Language::Unknown
    },
    {
      "separator_short_alpha_islands_unknown",
      "He********************************o",
      Language::Unknown
    },
    {
      "repeated_digits_unknown",
      "111111111111111",
      Language::Unknown
    },
    {
      "repeated_letters_unknown",
      "aaaaaaaaaaaaaaa",
      Language::Unknown
    },
    {"codeish_unknown", "foo::bar->baz", Language::Unknown},
    {"unknown_short", "ok", Language::Unknown},
    // --- Email: practical valid cases ---
    {"email_plus_tag", "user+tag@example.com", Language::Email},
    {"email_dots_local", "first.last@example.com", Language::Email},
    {"email_multiple_subdomains", "user@mail.sub.example.com", Language::Email},
    {"email_hyphen_domain", "user@my-domain.example", Language::Email},
    {"email_punycode_domain", "user@xn--d1acpjx3f.xn--p1ai", Language::Email},

    // --- Email: local part edge rules ---
    {"email_local_leading_dot_invalid", ".user@example.com", Language::Unknown},
    {"email_local_trailing_dot_invalid", "user.@example.com", Language::Unknown},
    {"email_local_consecutive_dots_invalid", "user..name@example.com", Language::Unknown},

    // --- Email: domain edge rules ---
    {"email_domain_leading_dot_invalid", "user@.example.com", Language::Unknown},
    {"email_domain_invalid_chars", "user@exa!mple.com", Language::Unknown},

    // --- Email: TLD variations ---
    {"email_short_tld_invalid", "user@example.c", Language::Unknown},
    {"email_hyphen_tld_valid", "user@example.xn--p1ai", Language::Email},

    // --- Email: invalid structure ---
    {"email_trailing_dot_domain_invalid", "user@example.com.", Language::Unknown},
    {"email_localhost_invalid", "user@localhost", Language::Unknown},
    {"email_numeric_domain_label_invalid", "user@server1", Language::Unknown},
    {"email_numeric_tld_invalid", "user@example.123", Language::Unknown},
    {"email_mixed_tld_invalid", "user@example.c0m", Language::Unknown},
    {"email_missing_at", "userexample.com", Language::Unknown},
    {"email_multiple_at", "user@@example.com", Language::Unknown},
    {"email_empty_local", "@example.com", Language::Unknown},
    {"email_empty_domain", "user@", Language::Unknown},
    {"path_with_at_not_email", "/plb/opb/serial@ef600500", Language::FilePath},
    {"display_mode_at_not_email", "1024x768-32@60", Language::Unknown},
    {"device_tree_node_at_not_email", "rtas@0", Language::Unknown},

    // --- Email: unsupported but RFC-valid (documented exclusions) ---
    {"email_quoted_local", "\"user name\"@example.com", Language::Unknown},
    {"email_ip_literal", "user@[192.168.1.1]", Language::Unknown},
    {"email_ipv6_literal", "user@[IPv6:2001:db8::1]", Language::Unknown},
    {"email_unicode_local", "pelé@example.com", Language::Unknown},
    {"email_unicode_domain", "user@例子.公司", Language::Unknown},
  };

  int failures = 0;
  for (const auto &test: tests)
    failures += RunCase(test);

  if (failures != 0) {
    std::cerr << failures << " language detection test(s) failed.\n";
    return 1;
  }

  std::cout << "All language detection tests passed (" << tests.size() << " cases).\n";
  return 0;
}
