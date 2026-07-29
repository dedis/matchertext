package matchertext_test

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/matchertext"
)

// srcPath is the file under test, reached from this package directory.
//
// MATCHERTEXT_SRC points it elsewhere. That exists so a deliberately broken
// copy can be run through this suite, which is the only way to know the tests
// would catch a regression rather than merely agreeing with correct code.
var srcPath = func() string {
	if p := os.Getenv("MATCHERTEXT_SRC"); p != "" {
		return p
	}
	return filepath.Join("..", "..", "sqlite", "src", "matchertext.c")
}()

var driverPath string

func TestMain(m *testing.M) {
	dir, err := os.MkdirTemp("", "matchertext-test")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer os.RemoveAll(dir)

	bin, out, err := buildDriver(dir, "driver", "-std=c99", "-O2")
	if err != nil {
		fmt.Fprintf(os.Stderr, "cannot build the test driver: %v\n%s\n", err, out)
		os.RemoveAll(dir)
		os.Exit(1)
	}
	driverPath = bin

	code := m.Run()
	os.RemoveAll(dir)
	os.Exit(code)
}

// compiler reports the C compiler to use.
func compiler() string {
	if cc := os.Getenv("CC"); cc != "" {
		return cc
	}
	return "cc"
}

// buildDriver copies the file under test and the stub header into dir and
// compiles them there together with driver.c. Copying rather than compiling in
// place keeps the SQLite tree untouched and lets the stub header win the
// quoted include, which would otherwise resolve to the real sqliteInt.h next
// to matchertext.c and drag in a full build.
//
// The file under test is copied byte for byte, so what runs is what is
// committed.
func buildDriver(dir, name string, cflags ...string) (string, string, error) {
	for _, f := range []struct{ from, to string }{
		{srcPath, filepath.Join(dir, "matchertext.c")},
		{filepath.Join("csrc", "sqliteInt.h"), filepath.Join(dir, "sqliteInt.h")},
	} {
		b, err := os.ReadFile(f.from)
		if err != nil {
			return "", "", err
		}
		if err := os.WriteFile(f.to, b, 0o644); err != nil {
			return "", "", err
		}
	}

	bin := filepath.Join(dir, name)
	args := append([]string{}, cflags...)
	args = append(args,
		"-I", dir,
		"-DSQLITE_ENABLE_MATCHERTEXT",
		"-o", bin,
		filepath.Join(dir, "matchertext.c"),
		filepath.Join("csrc", "driver.c"),
	)
	out, err := exec.Command(compiler(), args...).CombinedOutput()
	return bin, string(out), err
}

// run feeds cmds to the driver and returns one result line per command.
func run(t *testing.T, cmds []string) []string {
	t.Helper()
	if len(cmds) == 0 {
		return nil
	}

	var in bytes.Buffer
	for _, c := range cmds {
		in.WriteString(c)
		in.WriteByte('\n')
	}

	cmd := exec.Command(driverPath)
	cmd.Stdin = &in
	var out, errb bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errb
	if err := cmd.Run(); err != nil {
		t.Fatalf("driver failed: %v\nstderr: %s", err, errb.String())
	}

	lines := strings.Split(strings.TrimSuffix(out.String(), "\n"), "\n")
	if len(lines) != len(cmds) {
		t.Fatalf("driver returned %d results for %d commands", len(lines), len(cmds))
	}
	return lines
}

func verifyCmd(b []byte) string { return "V " + hex.EncodeToString(b) }
func endCmd(b []byte) string    { return "E " + hex.EncodeToString(b) }

func parseInt(t *testing.T, s string) int64 {
	t.Helper()
	n, err := strconv.ParseInt(s, 10, 64)
	if err != nil {
		t.Fatalf("driver returned %q, which is not a number", s)
	}
	return n
}

// oracleVerify decides membership using the independent Go implementation.
// UnmatchedOffsets returns the offsets of unmatched matchers, so an empty
// result is exactly the statement that the input is matchertext.
func oracleVerify(b []byte) bool {
	offs, err := matchertext.UnmatchedOffsets(bytes.NewReader(b))
	if err != nil {
		panic(err)
	}
	return len(offs) == 0
}

func closerOf(o byte) (byte, bool) {
	switch o {
	case '(':
		return ')', true
	case '[':
		return ']', true
	case '{':
		return '}', true
	}
	return 0, false
}

// oracleEnd computes the end of an embedding by brute force from the oracle:
// the least i at which the enclosed value z[1:i] is matchertext and z[i]
// closes z[0]. Returns 0 when no such position exists.
//
// This is the definition FINDEMBEDEND is an efficient implementation of, so
// agreement between the two is the property worth testing. It costs a scan per
// candidate position and is therefore only fit for short inputs.
func oracleEnd(z []byte) int64 {
	c, ok := closerOf(z[0])
	if !ok {
		panic("oracleEnd requires an opener")
	}
	for i := 1; i < len(z); i++ {
		if z[i] == c && oracleVerify(z[1:i]) {
			return int64(i + 1)
		}
	}
	return 0
}

func TestVerifyGoldenCases(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want bool
	}{
		// The empty string is matchertext, by the flat rule with no characters.
		{"empty", "", true},
		{"nonmatchers only", "hello world", true},

		// Each pair, then nesting and interleaving.
		{"parentheses", "(a)", true},
		{"brackets", "[a]", true},
		{"braces", "{a}", true},
		{"nested", "((a))", true},
		{"interleaved", "([{}])", true},
		{"sequential", "a(b)c[d]e{f}g", true},
		{"empty pairs", "()[]{}", true},

		// Angle brackets are not matchers, so they constrain nothing. This is
		// deliberate: they are used unmatched as inequality operators far too
		// often to be included.
		{"angle brackets unmatched", "a < b > c", true},

		// The payloads that motivate the work carry no matchers at all, so they
		// are matchertext and embed inertly rather than being rejected. Nothing
		// about them is treated as special.
		{"sql tautology", "' OR '1'='1", true},
		{"sql comment", "x' OR 1=1 --", true},
		{"sql stacked", "'; DROP TABLE users; --", true},
		{"xss balanced", "<script>alert(1)</script>", true},
		{"xss img", "<img src=x onerror=steal()>", true},
		{"ldap wildcard", "*)(uid=*", false},

		// Unmatched matchers in either direction.
		{"lone open", "(", false},
		{"lone close", ")", false},
		{"close then open", ")(", false},
		{"trailing close", "a)b", false},
		{"unclosed call", "f(x", false},
		{"emoticon", "smile :]", false},

		// Correctly paired but of the wrong kind, which the discipline rejects.
		{"wrong kind", "(]", false},
		{"crossed", "([)]", false},
		{"wrong closer nested", "([a)]", false},

		// Zero bytes are ordinary nonmatchers. VERIFY takes an explicit length
		// for this reason.
		{"zero byte inside", "a\x00(b)", true},
		{"zero byte and imbalance", "a\x00(b", false},

		// Multi-byte UTF-8 is scanned safely because no continuation byte can
		// collide with a matcher.
		{"utf8 text", "caf\u00e9 (na\u00efve)", true},
		{"utf8 imbalanced", "caf\u00e9 (na\u00efve", false},

		// U+FF3B FULLWIDTH LEFT SQUARE BRACKET is not an ASCII matcher, so it
		// passes. It normalizes to a lone "[" under NFKC, which is why the
		// caller must present the value already canonicalized: this test
		// records the boundary of what the scanner can be responsible for.
		{"fullwidth bracket passes", "\uff3b", true},
	}

	cmds := make([]string, len(cases))
	for i, c := range cases {
		cmds[i] = verifyCmd([]byte(c.in))
	}
	got := run(t, cmds)

	for i, c := range cases {
		want := "0"
		if c.want {
			want = "1"
		}
		if got[i] != want {
			t.Errorf("%s: Verify(%q) = %s, want %s", c.name, c.in, got[i], want)
		}
		// The oracle must agree with the hand-written answer, otherwise the
		// differential fuzz below is measuring against the wrong thing.
		if oracleVerify([]byte(c.in)) != c.want {
			t.Errorf("%s: the Go oracle disagrees with the expected value for %q", c.name, c.in)
		}
	}
}

func TestEndGoldenCases(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want int64
	}{
		// The value is everything between the opener and its matching closer,
		// so the return value is that length plus the two delimiters.
		{"empty value", "()", 2},
		{"one character", "(a)", 3},
		{"nested", "((a))", 5},
		{"brackets", "[a]", 3},
		{"braces", "{a}", 3},
		{"mixed nesting", "([{}])", 6},

		// Trailing text is not consumed. The caller decides what follows.
		{"trailing text", "(a)bcd", 3},
		{"trailing quote", "(a)'", 3},

		// The point of the exercise: the value may hold the host's own
		// terminators, because the end is found by balance and not by
		// scanning for a closing quote.
		{"value holds quotes", "(' OR '1'='1)", 13},
		{"value holds semicolon", "('; DROP TABLE t; --)", 21},
		{"value holds a closing quote", "(it's)", 6},

		// The first closer that is not matched from within ends the embedding,
		// so a later one is outside it.
		{"boundary at first free closer", "(a)b)", 3},

		// Rejections. Each fails closed: no embedding ends here.
		{"wrong kind at the boundary", "(a]b)", 0},
		{"immediate wrong closer", "(}", 0},
		{"unterminated", "(abc", 0},
		{"unclosed inner", "(a(b", 0},
		{"crossed", "([)]", 0},
		{"opener alone", "(", 0},
	}

	cmds := make([]string, len(cases))
	for i, c := range cases {
		cmds[i] = endCmd([]byte(c.in))
	}
	got := run(t, cmds)

	for i, c := range cases {
		if n := parseInt(t, got[i]); n != c.want {
			t.Errorf("%s: End(%q) = %d, want %d", c.name, c.in, n, c.want)
		}
		if n := oracleEnd([]byte(c.in)); n != c.want {
			t.Errorf("%s: the oracle gives %d for %q, want %d", c.name, n, c.in, c.want)
		}
	}
}

// TestEndRecoversTheValue checks the property the caller actually relies on:
// what sits between the delimiters that End reports is itself matchertext, and
// is returned unchanged. Nothing is escaped, unescaped or rewritten.
func TestEndRecoversTheValue(t *testing.T) {
	cases := []string{
		"(' OR '1'='1)",
		"([nested] and {more})",
		"(a)",
		"()",
		"(\u00e9\u00e0\u00fc)",
	}

	cmds := make([]string, len(cases))
	for i, c := range cases {
		cmds[i] = endCmd([]byte(c))
	}
	got := run(t, cmds)

	for i, c := range cases {
		n := parseInt(t, got[i])
		if n < 2 {
			t.Fatalf("End(%q) = %d, expected an embedding", c, n)
		}
		value := c[1 : n-1]
		if !oracleVerify([]byte(value)) {
			t.Errorf("End(%q) delimited %q, which is not matchertext", c, value)
		}
		// The delimiters are exactly the opener and its partner.
		if closer, _ := closerOf(c[0]); c[n-1] != closer {
			t.Errorf("End(%q) ended on %q, not the matching closer", c, c[n-1])
		}
	}
}

// TestDepthLimit pins the behaviour at SQLITE_MAX_MATCHER_DEPTH. Input nested
// more deeply is rejected rather than truncated or accepted, which keeps the
// failure closed: the value does not embed and the statement does not parse.
//
// The budget is spent on the region actually scanned. VERIFY scans the whole
// string, so it counts every level. FINDEMBEDEND begins one byte past the
// opener, because the caller has already consumed that delimiter, so the
// opener is not on the stack and does not count. The consequence is worth
// stating plainly: an embedding whose value sits exactly at the limit is
// accepted, and its total nesting is therefore limit+1. The two entry points
// agree on the language and disagree on what "depth" is measured over.
func TestDepthLimit(t *testing.T) {
	const limit = 1000

	// nest(d) has matcher depth d.
	nest := func(d int) string {
		return strings.Repeat("(", d) + strings.Repeat(")", d)
	}
	// embed(d) delimits a value of depth d, so it has total depth d+1.
	embed := func(d int) string { return nest(d + 1) }

	cmds := []string{
		verifyCmd([]byte(nest(limit))),
		verifyCmd([]byte(nest(limit + 1))),
		endCmd([]byte(embed(limit))),
		endCmd([]byte(embed(limit + 1))),
	}
	got := run(t, cmds)

	if got[0] != "1" {
		t.Errorf("Verify of depth %d = %s, want 1 (at the limit)", limit, got[0])
	}
	if got[1] != "0" {
		t.Errorf("Verify of depth %d = %s, want 0 (over the limit)", limit+1, got[1])
	}
	if n, want := parseInt(t, got[2]), int64(2*(limit+1)); n != want {
		t.Errorf("End of a value of depth %d = %d, want %d (at the limit)",
			limit, n, want)
	}
	if n := parseInt(t, got[3]); n != 0 {
		t.Errorf("End of a value of depth %d = %d, want 0 (over the limit)",
			limit+1, n)
	}

	// The oracle has no depth limit, so it accepts what the C code rejects.
	// That is the intended difference and is recorded here rather than left
	// for the fuzz to trip over.
	if !oracleVerify([]byte(nest(limit + 1))) {
		t.Errorf("the oracle should accept depth %d", limit+1)
	}
}

// alphabets are weighted so that matchers are common. Random bytes would
// almost never produce an imbalance worth finding.
var (
	verifyAlphabet = [][]byte{
		[]byte("("), []byte(")"), []byte("["), []byte("]"), []byte("{"), []byte("}"),
		[]byte("a"), []byte("b"), []byte("'"), []byte("\""), []byte(";"),
		[]byte("<"), []byte(">"), []byte("\x00"),
		[]byte("\u00e9"), []byte("\uff3b"),
	}
	// FINDEMBEDEND reads to a zero byte, so cases for it carry none.
	endAlphabet = [][]byte{
		[]byte("("), []byte(")"), []byte("["), []byte("]"), []byte("{"), []byte("}"),
		[]byte("a"), []byte("'"), []byte("\u00e9"),
	}
)

func randCase(rng *rand.Rand, alphabet [][]byte, maxTokens int) []byte {
	n := rng.Intn(maxTokens + 1)
	var b []byte
	for i := 0; i < n; i++ {
		b = append(b, alphabet[rng.Intn(len(alphabet))]...)
	}
	return b
}

func fuzzCount(short int, long int) int {
	if testing.Short() {
		return short
	}
	return long
}

// TestVerifyAgainstOracle is the differential run. The C scanner and the Go
// implementation share no code, so agreement across a large random sample is
// evidence that both decide the same language, and any disagreement localizes
// a bug in one of them.
func TestVerifyAgainstOracle(t *testing.T) {
	rng := rand.New(rand.NewSource(1))
	n := fuzzCount(5000, 200000)

	inputs := make([][]byte, n)
	cmds := make([]string, n)
	for i := range inputs {
		inputs[i] = randCase(rng, verifyAlphabet, 12)
		cmds[i] = verifyCmd(inputs[i])
	}
	got := run(t, cmds)

	nValid, nBad := 0, 0
	for i, in := range inputs {
		want := "0"
		if oracleVerify(in) {
			want = "1"
			nValid++
		}
		if got[i] != want {
			nBad++
			if nBad <= 10 {
				t.Errorf("Verify(%q) = %s, oracle says %s", in, got[i], want)
			}
		}
	}
	if nBad > 10 {
		t.Errorf("%d disagreements in total", nBad)
	}

	// A sample that was almost all valid, or almost none, would pass without
	// exercising much. Record the mix so a future change to the alphabet
	// cannot quietly weaken the test.
	if nValid == 0 || nValid == n {
		t.Fatalf("degenerate sample: %d of %d valid", nValid, n)
	}
	t.Logf("%d cases, %d valid matchertext, %d rejected", n, nValid, n-nValid)
}

// TestEndAgainstOracle checks FINDEMBEDEND against the definition it
// implements, computed independently through the Go oracle. This is the
// operational content of embed_boundary.lean.
func TestEndAgainstOracle(t *testing.T) {
	rng := rand.New(rand.NewSource(2))
	n := fuzzCount(5000, 100000)

	var inputs [][]byte
	var cmds []string
	for len(inputs) < n {
		b := randCase(rng, endAlphabet, 12)
		// The contract is that the caller has already established an opener at
		// z[0]; the C code asserts it.
		if len(b) == 0 {
			continue
		}
		if _, ok := closerOf(b[0]); !ok {
			continue
		}
		inputs = append(inputs, b)
		cmds = append(cmds, endCmd(b))
	}
	got := run(t, cmds)

	nFound, nBad := 0, 0
	for i, in := range inputs {
		want := oracleEnd(in)
		if want > 0 {
			nFound++
		}
		g := parseInt(t, got[i])
		if g != want {
			nBad++
			if nBad <= 10 {
				t.Errorf("End(%q) = %d, oracle says %d", in, g, want)
			}
			continue
		}
		// Where an embedding was found, the delimited value must be
		// matchertext and the closer must match the opener.
		if g > 0 {
			closer, _ := closerOf(in[0])
			if in[g-1] != closer {
				t.Errorf("End(%q) ended on %q, not %q", in, in[g-1], closer)
			}
			if !oracleVerify(in[1 : g-1]) {
				t.Errorf("End(%q) delimited %q, which is not matchertext", in, in[1:g-1])
			}
		}
	}
	if nBad > 10 {
		t.Errorf("%d disagreements in total", nBad)
	}
	if nFound == 0 || nFound == len(inputs) {
		t.Fatalf("degenerate sample: %d of %d located an embedding", nFound, len(inputs))
	}
	t.Logf("%d openers, %d embeddings located, %d rejected",
		len(inputs), nFound, len(inputs)-nFound)
}

// TestBuildIsWarningFree compiles the file under test under both language
// standards SQLite supports, with warnings promoted to errors. The coding
// rules forbid C99-only constructs, and a warning here is the cheapest place
// to catch one.
func TestBuildIsWarningFree(t *testing.T) {
	for _, std := range []string{"-std=c89", "-std=c99"} {
		for _, opt := range []string{"-O0", "-O2"} {
			t.Run(std+" "+opt, func(t *testing.T) {
				dir := t.TempDir()
				_, out, err := buildDriver(dir, "warn",
					std, opt, "-Wall", "-Wextra", "-Werror")
				if err != nil {
					t.Errorf("build failed under %s %s:\n%s", std, opt, out)
				} else if strings.TrimSpace(out) != "" {
					t.Errorf("build produced output under %s %s:\n%s", std, opt, out)
				}
			})
		}
	}
}

// TestBuildsWithoutTheFlag checks that the file is inert when
// SQLITE_ENABLE_MATCHERTEXT is not defined, which is what makes the change
// conservative: with the flag off there is nothing to compile and nothing to
// go wrong.
func TestBuildsWithoutTheFlag(t *testing.T) {
	dir := t.TempDir()
	b, err := os.ReadFile(srcPath)
	if err != nil {
		t.Fatal(err)
	}
	src := filepath.Join(dir, "matchertext.c")
	if err := os.WriteFile(src, b, 0o644); err != nil {
		t.Fatal(err)
	}

	out, err := exec.Command(compiler(),
		"-std=c89", "-Wall", "-Wextra", "-Werror",
		"-c", src, "-o", filepath.Join(dir, "off.o"),
	).CombinedOutput()
	if err != nil {
		t.Errorf("the file does not compile with the flag off:\n%s", out)
	}

	// With the flag off nothing should be defined, so the object exports no
	// symbols of ours. Checked by absence of a link-time definition rather
	// than by parsing nm output, which varies by platform.
	if strings.Contains(string(out), "warning") {
		t.Errorf("unexpected warnings with the flag off:\n%s", out)
	}
}
