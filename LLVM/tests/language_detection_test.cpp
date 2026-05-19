#include <iostream>
#include <string_view>
#include <vector>

#include "LanguageClassifier.hpp"

namespace {
  struct TestCase {
    const char *name;
    std::string_view input;
    LanguageEnum expected;
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
    {"url", "https://example.com/api/v1/items?id=42", LanguageEnum::URL},
    {"data_url", "data:text/plain;base64,SGVsbG8=", LanguageEnum::URL},
    {"pseudo_url_with_newline_wrappers", "\\nhttp://www.attotech.com\\n\\n", LanguageEnum::PseudoURL},
    {"email", "thomas@kaiser-linux.li", LanguageEnum::Email},
    {
      "pseudo_email_contact", "Thomas Kaiser <thomas@kaiser-linux.li>",
      LanguageEnum::PseudoEmail
    },
    {
      "pseudo_email_label", "Author: samr7@cs.washington.edu",
      LanguageEnum::PseudoEmail
    },
    {
      "wrapped_url_contact", "Jean-Francois Moine <http://moinejf.free.fr>",
      LanguageEnum::PseudoURL
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
      LanguageEnum::PseudoURL
    },
    {"data_prefix_format_not_url", "data: %8ph", LanguageEnum::FormatString},
    {
      "data_prefix_plain_text_not_url", "data: complete",
      LanguageEnum::PlainText
    },
    {"file_path", "/usr/local/include/project/config.yaml", LanguageEnum::FilePath},
    {"relative_file_path", "../src/LanguageClassifier.cpp", LanguageEnum::FilePath},
    {"repo_header_path", "base/uuid.h", LanguageEnum::FilePath},
    {"repo_nested_header_path", "chrome/browser/ai/features.h", LanguageEnum::FilePath},
    {
      "repo_generated_header_path",
      "mojo/public/cpp/bindings/tests/fixed_array_size_unittest.test-mojom-shared.h",
      LanguageEnum::FilePath
    },
    {
      "windows_file_path",
      R"(C:\Program Files\MatcherText\config\settings.json)",
      LanguageEnum::FilePath
    },
    {"format_string", "user=%s count=%d", LanguageEnum::FormatString},
    {
      "complex_format_string", "[%s] user=%s retries=%d code=%x",
      LanguageEnum::FormatString
    },
    {
      "escaped_percent_format_string", "progress=%d%% owner=%s",
      LanguageEnum::FormatString
    },
    {
      "width_length_format_string", "  LD_LOCK                        = %16lx\\n",
      LanguageEnum::FormatString
    },
    {
      "sql", "SELECT id, name FROM users WHERE active = 1 ORDER BY name",
      LanguageEnum::SQL
    },
    {
      "comment_wrapped_sql",
      "/* SELECT id, payload FROM logs WHERE level = 'warn' */",
      LanguageEnum::SQL
    },
    {
      "cte_sql",
      "WITH recent AS (SELECT id, level FROM logs) SELECT * FROM recent WHERE level = 'warn'",
      LanguageEnum::SQL
    },
    {
      "sql_leader_without_secondary_keyword", "SELECT id",
      LanguageEnum::Unknown
    },
    {
      "escaped_json", "{\\\"items\\\": [\\\"one\\\", \\\"two\\\"], \\\"count\\\": 2}",
      LanguageEnum::JSON
    },
    {
      "json_array",
      "[{\"name\": \"one\"}, {\"name\": \"two\"}]",
      LanguageEnum::JSON
    },
    {
      "multiline_escaped_json",
      "{\\n  \\\"meta\\\": {\\\"count\\\": 2},\\n  \\\"items\\\": [\\\"one\\\", \\\"two\\\"]\\n}",
      LanguageEnum::JSON
    },
    {
      "single_quoted_json_like_object",
      "{'type': 'number'}",
      LanguageEnum::JSON
    },
    {
      "yaml",
      "name: matchertext\nversion: 1\nitems:\n  - one\n  - two",
      LanguageEnum::YAML
    },
    {
      "comment_wrapped_yaml",
      "/*\nservice: matchertext\nretries: 3\npaths:\n  - src\n*/",
      LanguageEnum::YAML
    },
    {
      "yaml_document",
      "---\nservice: matchertext\nretries: 3\npaths:\n  - src\n  - include\n...",
      LanguageEnum::YAML
    },
    {
      "yaml_false_positive_error_hints",
      "Error: Unable to find debugfs/tracefs\nHint: Was your kernel compiled with debugfs/tracefs support?\nHint: Is the debugfs/tracefs filesystem mounted?",
      LanguageEnum::PlainText
    },
    {
      "yaml_false_positive_counter_dump",
      "over: 0\ncount: 0\nmin: 0\navg: 0\nmax: 0\n",
      LanguageEnum::PlainText
    },
    {
      "yaml_false_positive_numeric_labels",
      "0: generic\n1: Trust 120 SpaceCam\n2: other Trust 120 SpaceCam\n",
      LanguageEnum::PlainText
    },
    {
      "yaml_false_positive_assembly_labels",
      "2: ldbu  r2,0(r5)\n9: stb   r2,0(r3)\n",
      LanguageEnum::Unknown
    },
    {
      "html",
      "<!doctype html><html><body><div class=\"hero\">Hi</div></body></html>",
      LanguageEnum::HTML
    },
    {
      "html_with_script_and_style",
      "<div><script>console.log('x')</script><style>.hero { color: red; }</style></div>",
      LanguageEnum::HTML
    },
    {
      "xml",
      "<?xml version=\"1.0\"?><xsl:stylesheet version=\"1.0\"></xsl:stylesheet>",
      LanguageEnum::XML
    },
    {
      "namespaced_xml",
      "<xsl:stylesheet version=\"1.0\"><xsl:template match=\"/\"/></xsl:stylesheet>",
      LanguageEnum::XML
    },
    {
      "namespaced_self_closing_xml",
      "<xi:include href=\"riscv-64bit-cpu.xml\"/>",
      LanguageEnum::XML
    },
    {
      "xml_false_positive_url_in_angle_brackets",
      "Jean-Francois Moine <http://moinejf.free.fr>",
      LanguageEnum::PseudoURL
    },
    {
      "xml_false_positive_rust_path",
      "<std::path::PathBuf>::new",
      LanguageEnum::Unknown
    },
    {
      "xml_false_positive_format_placeholder",
      "<%s:%d>\\n",
      LanguageEnum::FormatString
    },
    {
      "xml_false_positive_single_colon_placeholder",
      "<decode:run>",
      LanguageEnum::Unknown
    },
    {"css", ".hero { color: red; margin: 0; padding: 4px; }", LanguageEnum::CSS},
    {"regex", "^(?:foo|bar)\\d{2,4}$", LanguageEnum::Regex},
    {
      "named_group_regex",
      "^(?P<name>[A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(?:true|false)$",
      LanguageEnum::Regex
    },
    {
      "shell", "grep -R \"needle\" src | sed 's/foo/bar/' > out.txt",
      LanguageEnum::Shell
    },
    {"shell_shebang", "#!/bin/sh\necho hello", LanguageEnum::Shell},
    {
      "shell_with_vars",
      "find src -name '*.cpp' | xargs grep -n \"$TOKEN\" 2>/dev/null",
      LanguageEnum::Shell
    },
    {
      "inline_asm_operands", "\t%0, %1, %3                          \\n",
      LanguageEnum::InlineAsm
    },
    {
      "inline_asm_directives",
      " ,\\\"a\\\"; \\n.balign 4, 0;                            \\n.popsection;                             \\n",
      LanguageEnum::InlineAsm
    },
    {
      "identifier_like", "ProcessBoundString::EncryptBuffer",
      LanguageEnum::IdentifierLike
    },
    {
      "cpp_reference_declaration_fragment",
      "Phone   &number  ",
      LanguageEnum::CPP
    },
    {
      "resource_identifier",
      "metrics/ui.startup/FirstContentfulPaint",
      LanguageEnum::Unknown
    },
    {
      "slash_identifier_without_extension",
      "foo/bar/baz",
      LanguageEnum::Unknown
    },
    {
      "plain_text", "This function returns the current device state.",
      LanguageEnum::PlainText
    },
    {
      "comment_wrapped_plain_text",
      "// This subsystem schedules jobs and retries transient failures automatically.",
      LanguageEnum::PlainText
    },
    {
      "multiline_plain_text",
      "This subsystem schedules jobs.\nIt retries transient failures automatically.",
      LanguageEnum::PlainText
    },
    {
      "plain_text_label_configuration_write", "Configuration Write",
      LanguageEnum::PlainText
    },
    {
      "plain_text_label_collecting_samples", "Collecting samples...",
      LanguageEnum::PlainText
    },
    {
      "plain_text_label_system_sample_rate", "System Sample Rate",
      LanguageEnum::PlainText
    },
    {
      "plain_text_label_namespace_node", " Namespace Node - ",
      LanguageEnum::PlainText
    },
    {
      "plain_text_label_mic_type_select", "Mic Type Select",
      LanguageEnum::PlainText
    },
    {
      "plain_text_label_configuring_gpio", "Configuring GPIO\\n",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_preview_dump", "---- Preview Register dump ----",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_slice_banner", "----------- slice ---------",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_time_banner",
      "------------------------------------------\\n\\t\\tTime\\n------------------------------------------\\n",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_skb_dump",
      "\\n************** SKB dump ****************\\n",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_btcoex_banner",
      "[BTCoex], ****************************************************************\\n",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_end_banner",
      "***********************END***********************\\n",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_q40_reset",
      "*******************************************\\nCalled q40_reset : press the RESET button!!\\n*******************************************\\n",
      LanguageEnum::PlainText
    },
    {
      "decorated_plain_text_sync_banner", "******* SYNC *******",
      LanguageEnum::PlainText
    },
    {
      "decorated_separator_capsule_unknown", "o-----------------------o\\n",
      LanguageEnum::Unknown
    },
    {"hex_data", "4d5a90000300000004000000ffff0000", LanguageEnum::HexData},
    {
      "grouped_hex_data",
      "4d5a9000:03000000:04000000:ffff0000",
      LanguageEnum::HexData
    },
    {
      "binary_data", "\\x89PNG\\x0d\\x0a\\x1a\\x0a\\x00\\x00\\x00\\x0dIHDR",
      LanguageEnum::PseudoBinaryData
    },
    {
      "pseudo_binary_data",
      "Selected buyer record\\n\\x01\\x00\\x00\\x00\\x08s\\x00\\x01\\nbsid\\nid",
      LanguageEnum::PseudoBinaryData
    },
    {
      "pseudo_binary_data_text_prefix",
      "test_\\001\\002\\003\\n\\r",
      LanguageEnum::PseudoBinaryData
    },
    {
      "pseudo_binary_data_ip_prefix",
      "1.2.3.4\\xF0\\x9F\\x92\\xA9",
      LanguageEnum::PseudoBinaryData
    },
    {
      "pseudo_binary_data_html_wrapper",
      "<b>\\xF0\\x9F\\x8F\\xAB</b>",
      LanguageEnum::PseudoBinaryData
    },
    {
      "octal_binary_data", "\\000\\377\\123\\045\\000\\001\\002\\003",
      LanguageEnum::BinaryData
    },
    {
      "threshold_rejects_url", "https://example.com/api/v1/items?id=42",
      LanguageEnum::Unknown, 0.99f
    },
    {
      "separator_stars_unknown", "**************************************\\n",
      LanguageEnum::Unknown
    },
    {
      "separator_dashes_unknown", "--------------------------------------------\\n",
      LanguageEnum::Unknown
    },
    {
      "separator_equals_unknown", "==========",
      LanguageEnum::Unknown
    },
    {
      "separator_short_alpha_islands_unknown",
      "He********************************o",
      LanguageEnum::Unknown
    },
    {
      "repeated_digits_unknown",
      "111111111111111",
      LanguageEnum::Unknown
    },
    {
      "repeated_letters_unknown",
      "aaaaaaaaaaaaaaa",
      LanguageEnum::Unknown
    },
    {"codeish_unknown", "foo::bar->baz", LanguageEnum::Unknown},
    {"unknown_short", "ok", LanguageEnum::Unknown},
    // --- Email: practical valid cases ---
    {"email_plus_tag", "user+tag@example.com", LanguageEnum::Email},
    {"email_dots_local", "first.last@example.com", LanguageEnum::Email},
    {"email_multiple_subdomains", "user@mail.sub.example.com", LanguageEnum::Email},
    {"email_hyphen_domain", "user@my-domain.example", LanguageEnum::Email},
    {"email_punycode_domain", "user@xn--d1acpjx3f.xn--p1ai", LanguageEnum::Email},

    // --- Email: local part edge rules ---
    {"email_local_leading_dot_invalid", ".user@example.com", LanguageEnum::Unknown},
    {"email_local_trailing_dot_invalid", "user.@example.com", LanguageEnum::Unknown},
    {"email_local_consecutive_dots_invalid", "user..name@example.com", LanguageEnum::Unknown},

    // --- Email: domain edge rules ---
    {"email_domain_leading_dot_invalid", "user@.example.com", LanguageEnum::Unknown},
    {"email_domain_invalid_chars", "user@exa!mple.com", LanguageEnum::Unknown},

    // --- Email: TLD variations ---
    {"email_short_tld_invalid", "user@example.c", LanguageEnum::Unknown},
    {"email_hyphen_tld_valid", "user@example.xn--p1ai", LanguageEnum::Email},

    // --- Email: invalid structure ---
    {"email_trailing_dot_domain_invalid", "user@example.com.", LanguageEnum::Unknown},
    {"email_localhost_invalid", "user@localhost", LanguageEnum::Unknown},
    {"email_numeric_domain_label_invalid", "user@server1", LanguageEnum::Unknown},
    {"email_numeric_tld_invalid", "user@example.123", LanguageEnum::Unknown},
    {"email_mixed_tld_invalid", "user@example.c0m", LanguageEnum::Unknown},
    {"email_missing_at", "userexample.com", LanguageEnum::Unknown},
    {"email_multiple_at", "user@@example.com", LanguageEnum::Unknown},
    {"email_empty_local", "@example.com", LanguageEnum::Unknown},
    {"email_empty_domain", "user@", LanguageEnum::Unknown},
    {"path_with_at_not_email", "/plb/opb/serial@ef600500", LanguageEnum::FilePath},
    {"display_mode_at_not_email", "1024x768-32@60", LanguageEnum::Unknown},
    {"device_tree_node_at_not_email", "rtas@0", LanguageEnum::Unknown},

    // --- Email: unsupported but RFC-valid (documented exclusions) ---
    {"email_quoted_local", "\"user name\"@example.com", LanguageEnum::Unknown},
    {"email_ip_literal", "user@[192.168.1.1]", LanguageEnum::Unknown},
    {"email_ipv6_literal", "user@[IPv6:2001:db8::1]", LanguageEnum::Unknown},
    {"email_unicode_local", "pelé@example.com", LanguageEnum::Unknown},
    {"email_unicode_domain", "user@例子.公司", LanguageEnum::Unknown},
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
