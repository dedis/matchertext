"""Unit tests of the labeling stages: taxonomy, nbayes, classify, subclass, and the shared helpers.

Run from injection-research/: python3 -m unittest discover -s test
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pipeline"))

import build_db  # noqa: E402
import classify  # noqa: E402
import matchertext  # noqa: E402
import sqli_experiment  # noqa: E402
import subclass  # noqa: E402
import taxonomy as tx  # noqa: E402
from nbayes import WordNB, features  # noqa: E402


class Taxonomy(unittest.TestCase):
    def test_rule_hyphen(self):
        self.assertEqual(tx.rule_match("An SQL-injection in the id parameter"), (tx.QUERY, "sql"))

    def test_rule_other_protocol_command(self):
        for d in ("AT command injection in the modem", "SMTP command injection via RCPT TO",
                  "IMAP command injection"):
            self.assertIsNone(tx.rule_match(d), d)

    def test_rule_shell_command(self):
        self.assertEqual(tx.rule_match("allows OS command injection"), (tx.COMMAND, "shell_command"))
        # "that" ends in "at", which must not read as the AT command set.
        self.assertEqual(tx.rule_match("a flaw that command injection exploits"), (tx.COMMAND, "shell_command"))

    def test_rule_host_header(self):
        self.assertIsNone(tx.rule_match("Host header injection allows cache poisoning"))
        self.assertEqual(tx.rule_match("HTTP header injection"), (tx.RENDERING, "crlf_header"))

    def test_rule_first_named(self):
        self.assertEqual(tx.rule_match("XSS, which also gives SQL injection")[1], "html_dom")
        self.assertEqual(tx.rule_match("SQL injection, which also gives XSS")[1], "sql")

    def test_mask_rules(self):
        self.assertNotIn("injection", tx.mask_rules("A SQL injection in the id field").lower())

    def test_labels_from_cwes(self):
        self.assertEqual(tx.labels_from_cwes([89], {}), (tx.QUERY, False, "sql"))
        self.assertEqual(tx.labels_from_cwes([77], {}), (tx.COMMAND, False, None))
        self.assertEqual(tx.labels_from_cwes([99], {}), (None, False, None))
        self.assertEqual(tx.labels_from_cwes([80], {80: [79]}), (tx.RENDERING, False, "html_dom"))
        # A generic CWE-74 is refined by a later, specific CWE.
        self.assertEqual(tx.labels_from_cwes([74, 89], {}), (tx.QUERY, False, "sql"))

    def test_whole_document(self):
        self.assertTrue(tx.whole_document("allows uploading an SVG file with a script"))
        self.assertFalse(tx.whole_document("XSS via the file name of an uploaded SVG"))


class NaiveBayes(unittest.TestCase):
    def test_bigrams(self):
        self.assertEqual(features("Union Select x"), ["union", "select", "x", "union select", "select x"])

    def test_predict(self):
        m = WordNB().fit(["union select password", "union select from users", "script alert onerror",
                          "img onerror alert"], ["sql", "sql", "xss", "xss"])
        label, margin = m.predict("union select")
        self.assertEqual(label, "sql")
        self.assertGreater(margin, 0)
        self.assertEqual(m.predict("onerror")[0], "xss")


class Classify(unittest.TestCase):
    def test_bucket_template(self):
        # One vendor's template for many CVEs, differing in digits, case, and spacing, shares a split.
        self.assertEqual(classify.bucket("Plugin X 1.2.3 allows XSS"), classify.bucket("plugin  x 4.5 allows   xss"))
        self.assertIn(classify.bucket("anything"), range(10))

    def test_mediated(self):
        self.assertEqual(classify.mediated("sql", "a text-to-SQL feature runs the query"), "llm_prompt")
        self.assertEqual(classify.mediated("sql", "SQL injection in the id parameter"), "sql")
        self.assertEqual(classify.mediated("shell_command", "prompt injection"), "shell_command")


class Subclass(unittest.TestCase):
    first = staticmethod(subclass.first)
    T, D = subclass.TECHNIQUE, subclass.TECHNIQUE_TEXT

    def test_sql_payload(self):
        self.assertEqual(self.first(self.T["sql"], "1 UNION SELECT 1,2"), "union_based")
        self.assertEqual(self.first(self.T["sql"], "' or '1'='1"), "tautology")
        self.assertEqual(self.first(self.T["sql"], "1 AND SLEEP(5)"), "blind_time")
        self.assertEqual(self.first(self.T["sql"], "1 and 1=1"), "blind_boolean")

    def test_shell_payload(self):
        self.assertEqual(self.first(self.T["shell_command"], "; ping -c 5 127.0.0.1"), "blind")
        self.assertEqual(self.first(self.T["shell_command"], "| id"), "separator")
        self.assertEqual(self.first(self.T["shell_command"], "$(id)"), "substitution")

    def test_sql_description(self):
        self.assertEqual(self.first(self.D["sql"], "blind SQL injection"), "blind")
        self.assertEqual(self.first(self.D["sql"], "time-based blind SQL injection"), "blind_time")
        self.assertEqual(self.first(self.D["sql"], "authentication bypass via SQL injection"), "auth_bypass")

    def test_prose_is_not_payload(self):
        self.assertIsNone(self.first(self.D["shell_command"], "OS command injection via the host field; the -o flag"))

    def test_persistence(self):
        self.assertEqual(self.first(self.D["html_dom"], "A non-persistent XSS"), "reflected")
        self.assertEqual(self.first(self.D["html_dom"], "A persistent XSS"), "stored")
        self.assertEqual(self.first(subclass.INJECTION_POINT, "non-persistent XSS via the q parameter"), "parameter")

    def test_cvss(self):
        self.assertEqual(subclass.cvss_facets("CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:C/C:L/I:L/A:N"),
                         {"privilege": "pre_auth", "attack_vector": "network", "user_interaction": "required"})
        self.assertEqual(subclass.cvss_facets("AV:N/AC:L/Au:S/C:P/I:P/A:P"),
                         {"privilege": "authenticated", "attack_vector": "network"})


class Shared(unittest.TestCase):
    def test_verify(self):
        v = matchertext.verify
        self.assertTrue(v("a(b[c]{d})e"))
        self.assertFalse(v("([)]"))
        self.assertFalse(v("a)b"))
        self.assertTrue(v("(" * 1000 + ")" * 1000))
        self.assertFalse(v("(" * 1001 + ")" * 1001))

    def test_edb_id(self):
        cases = {"https://www.exploit-db.com/exploits/19096": "19096",
                 "https://www.exploit-db.com/raw/25024": "25024",
                 "https://www.exploit-db.com/download/47655": "47655",
                 "http://www.exploit-db.com/exploit.php?id=10165": "10165",
                 "https://www.exploit-db.com/docs/47790": None,
                 "https://www.exploit-db.com/ghdb/4613/": None}
        for url, want in cases.items():
            m = build_db.EDB_ID_RE.search(url)
            self.assertEqual(m.group(1) if m else None, want, url)

    def test_like_rows(self):
        base = ("ok", "0", "S", "1", "0", "1", "-", "", "")
        more = ("ok", "0", "S", "1", "0", "3", "-", "", "")
        verdict = sqli_experiment.verdict
        self.assertEqual(verdict("concat", more, base, "%", False), "BREAKOUT")
        self.assertEqual(verdict("concat", more, base, "%", False, rows=False), "INERT")


if __name__ == "__main__":
    unittest.main()
