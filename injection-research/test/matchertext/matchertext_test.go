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
		"-DSQLITE_MATCHERTEXT_SCANNER_ONLY",
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
	if len(z) == 0 {
		return 0
	}
	c, ok := closerOf(z[0])
	if !ok {
		return 0 // no embedding begins at a byte that is not an opener
	}
	for i := 1; i < len(z); i++ {
		if z[i] == c && oracleVerify(z[1:i]) {
			return int64(i + 1)
		}
	}
	return 0
}

// The hand-written golden cases that used to sit here now live beside the code
// they describe, in the SQLite tree at test/matchertext.test, together with the
// tokenizer and parser tests that only make sense against a built SQLite.
//
// What stays here is what cannot move: the differential run against
// go/matchertext, which needs an implementation the SQLite tree does not
// contain, and the build checks, which compile the file on its own rather than
// as part of a library.

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

func encodeCmd(b []byte) string { return "C " + hex.EncodeToString(b) }
func decodeCmd(b []byte) string { return "D " + hex.EncodeToString(b) }

func unhex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("driver returned %q, which is not hex", s)
	}
	return b
}

// TestEncodeIsTotal is the property that makes the encoder worth having, and
// it is the C counterpart of toMatchertext_mt in to_matchertext.lean: whatever
// the input, the output is matchertext. VERIFY refuses; this cannot.
//
// The two further properties are what a caller relies on. Decoding an encoded
// value returns the original, byte for byte, so nothing is lost by passing a
// free-form field through the hole. And a value that is already matchertext
// and holds no backslash is left alone, which is what keeps the ordinary case
// legible in the SQL text.
func TestEncodeIsTotal(t *testing.T) {
	rng := rand.New(rand.NewSource(3))
	n := fuzzCount(5000, 100000)

	inputs := make([][]byte, n)
	cmds := make([]string, 0, 2*n)
	for i := range inputs {
		inputs[i] = randCase(rng, verifyAlphabet, 12)
		// A backslash has to appear, since escaping it is what makes the
		// encoding injective.
		if rng.Intn(4) == 0 {
			inputs[i] = append(inputs[i], '\\')
		}
		cmds = append(cmds, encodeCmd(inputs[i]))
	}
	enc := run(t, cmds)

	// Round trip each encoding back.
	back := make([]string, n)
	for i := range enc {
		back[i] = decodeCmd(unhex(t, enc[i]))
	}
	dec := run(t, back)

	nUntouched, nGrew := 0, 0
	for i, in := range inputs {
		e := unhex(t, enc[i])
		if !oracleVerify(e) {
			t.Fatalf("encode(%q) = %q, which the oracle says is not matchertext", in, e)
		}
		if d := unhex(t, dec[i]); !bytes.Equal(d, in) {
			t.Fatalf("round trip lost data: encode(%q) = %q, decode = %q", in, e, d)
		}
		if bytes.Equal(e, in) {
			nUntouched++
			if !oracleVerify(in) || bytes.ContainsRune(in, '\\') {
				t.Fatalf("encode(%q) left it alone but it needed escaping", in)
			}
		} else {
			nGrew++
		}
	}
	if nUntouched == 0 || nGrew == 0 {
		t.Fatalf("degenerate sample: %d untouched, %d rewritten", nUntouched, nGrew)
	}
	t.Logf("%d cases, %d already embeddable and left alone, %d escaped", n, nUntouched, nGrew)
}

// TestEncodeGoldenCases pins the escape alphabet itself. The escapes carry no
// matcher, which is the hypothesis hesc that to_matchertext.lean proves the
// output-is-matchertext theorem from.
func TestEncodeGoldenCases(t *testing.T) {
	cases := []struct{ name, in, want string }{
		{"empty", "", ""},
		{"no matchers", "hello", "hello"},
		{"balanced is untouched", "a(b)c", "a(b)c"},
		{"all three pairs balanced", "([{}])", "([{}])"},
		{"lone open paren", "(", `\o()`},
		{"lone close paren", ")", `\c()`},
		{"lone open bracket", "[", `\o[]`},
		{"lone close bracket", "]", `\c[]`},
		{"lone open brace", "{", `\o{}`},
		{"lone close brace", "}", `\c{}`},
		{"backslash doubles", `\`, `\\`},
		{"wrong kind escapes both", "(]", `\o()\c[]`},
		{"only the unmatched one", "(a)b)", `(a)b\c()`},
		{"emoticon", "smile :]", `smile :\c[]`},
		// The parenthesis, bracket and parenthesis here cross rather than nest,
		// so none of the three is matched and all three are escaped. Proper
		// nesting is the rule, not mere counting.
		{"crossed, so all escape", `printf("[")`, `printf\o()"\o[]"\c()`},
		{"nested properly, none escape", `printf("(x)")`, `printf("(x)")`},
		{"looks like an escape", `\o()`, `\\o()`},
	}

	cmds := make([]string, len(cases))
	for i, c := range cases {
		cmds[i] = encodeCmd([]byte(c.in))
	}
	got := run(t, cmds)

	for i, c := range cases {
		g := string(unhex(t, got[i]))
		if g != c.want {
			t.Errorf("%s: encode(%q) = %q, want %q", c.name, c.in, g, c.want)
		}
		if !oracleVerify([]byte(g)) {
			t.Errorf("%s: encode(%q) = %q, which is not matchertext", c.name, c.in, g)
		}
	}
}

// TestEncodeRespectsDepthLimit pins totality against the host's depth limit,
// not just against the unbounded language: the C scanner rejects nesting past
// SQLITE_MAX_MATCHER_DEPTH, so the encoder must never emit it. It caps real
// nesting one level below the limit, because an escape carries one balanced
// pair of its own, and escapes whatever would nest deeper. The check that
// matters is the C VERIFY accepting the encoder's output.
func TestEncodeRespectsDepthLimit(t *testing.T) {
	const limit = 1000 // SQLITE_MAX_MATCHER_DEPTH at its default
	nest := func(d int) []byte {
		b := make([]byte, 0, 2*d)
		for i := 0; i < d; i++ {
			b = append(b, '(')
		}
		for i := 0; i < d; i++ {
			b = append(b, ')')
		}
		return b
	}

	for _, tc := range []struct {
		name string
		d    int
	}{
		{"one below the limit", limit - 1},
		{"at the limit", limit},
		{"past the limit", limit + 1},
		{"far past the limit", 2 * limit},
	} {
		in := nest(tc.d)
		res := run(t, []string{encodeCmd(in)})
		enc := unhex(t, res[0])
		res = run(t, []string{verifyCmd(enc), decodeCmd(enc)})
		if res[0] != "1" {
			t.Errorf("%s: the C scanner rejects encode's output", tc.name)
		}
		if !bytes.Equal(unhex(t, res[1]), in) {
			t.Errorf("%s: round trip lost data", tc.name)
		}
		if tc.d < limit && !bytes.Equal(enc, in) {
			t.Errorf("%s: input below the cap was rewritten", tc.name)
		}
		if tc.d >= limit && bytes.Equal(enc, in) {
			t.Errorf("%s: input at or past the cap passed through", tc.name)
		}
	}
}
