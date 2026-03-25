#!/usr/bin/env python3
"""
Generate `include/LanguageModel.generated.hpp` from GitHub Linguist samples.

This script trains the trigram Naive Bayes tables used by
`src/LanguageClassifier.cpp`. The model is optimized for embedded strings, not
whole-file language detection: it extracts short, deterministic snippets that
resemble source literals, config fragments, markup, shell commands, SQL, and
ordinary prose.

Training pipeline:
  1. Extract embedded-like snippets from each supported Linguist language.
  2. Build `PlainText` from text corpora, READMEs, and real string literals.
  3. Normalize each file to a fixed trigram mass so large files do not dominate.
  4. Keep the most discriminative UTF-8 byte trigrams per language.
  5. Emit `LanguageModel.generated.hpp`, optionally printing holdout metrics.

Keep `LANGUAGE_MAP` aligned with the `Language` enum in
`include/LanguageClassifier.hpp`: the generated header stores those numeric ids
directly. If the trainable language set or enum values change, regenerate the
header before rebuilding the C++ targets.

Usage:
    python3 train/generate_model.py /path/to/linguist/samples \
        -o include/LanguageModel.generated.hpp
    python3 train/generate_model.py ignore/linguist/samples --eval-holdout 0.1

Requirements: Python 3.8+, no external dependencies.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import math
import os
import re
import sys
import textwrap
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path
from typing import Iterable, Iterator, Sequence

# ---------------------------------------------------------------------------
# Language mapping: Linguist directory name -> (C++ enum name, enum int value)
#
# These ids are emitted into the generated header and consumed directly by the
# C++ classifier, so they must stay in lock-step with `enum class Language` in
# include/LanguageClassifier.hpp.
# ---------------------------------------------------------------------------
LANGUAGE_MAP = {
    # Domain-specific
    "SQL": ("SQL", 5),
    "HTML": ("HTML", 6),
    "XML": ("XML", 7),
    "JSON": ("JSON", 8),
    "YAML": ("YAML", 9),
    "CSS": ("CSS", 10),
    "Shell": ("Shell", 12),
    # Programming languages
    "Python": ("Python", 13),
    "JavaScript": ("JavaScript", 14),
    "TypeScript": ("TypeScript", 15),
    "Java": ("Java", 16),
    "C": ("C", 17),
    "C++": ("CPP", 18),
    "C#": ("CSharp", 19),
    "Go": ("Go", 20),
    "Rust": ("Rust", 21),
    "Ruby": ("Ruby", 22),
    "PHP": ("PHP", 23),
    "Perl": ("Perl", 24),
    "Lua": ("Lua", 25),
    "Swift": ("Swift", 26),
    "Kotlin": ("Kotlin", 27),
    "R": ("R", 28),
    "Scala": ("Scala", 29),
    "Haskell": ("Haskell", 30),
    "OCaml": ("OCaml", 31),
    "Erlang": ("Erlang", 32),
    "Elixir": ("Elixir", 33),
    "Dart": ("Dart", 34),
    "Objective-C": ("Objective_C", 35),
    "GLSL": ("GLSL", 36),
    "HLSL": ("HLSL", 37),
}

TEXT_LANGUAGE_DIRS = ("Text", "Markdown", "reStructuredText", "AsciiDoc")

MARKUP_LANGUAGES = {"HTML", "XML"}
STRUCTURED_LANGUAGES = {"JSON", "YAML", "CSS", "SQL", "Shell"} | MARKUP_LANGUAGES

# Trigrams to keep per language.
# Higher values improve recall at the cost of a larger generated header.
TOP_N_TRIGRAMS = 15_000

# Snippet extraction parameters tuned for embedded strings rather than
# full-file classification.
MIN_SNIPPET_CHARS = 8
MAX_SNIPPET_CHARS = 512
TARGET_SNIPPET_LENGTHS = (12, 24, 48, 96, 192, 384)
WINDOW_LINE_COUNTS = (1, 2, 4, 8)
MAX_WINDOWS_PER_UNIT = 3
MAX_SNIPPETS_PER_FILE = 192
MAX_EVAL_SNIPPETS_PER_CLASS = 256
MAX_FILE_BYTES = 500_000  # skip only genuinely large outliers

# Per-file normalization and feature selection. The goal is to give each source
# file similar weight, then keep trigrams that separate a language from the
# rest of the corpus instead of merely favoring common tokens.
PER_FILE_TRIGRAM_MASS = 4096.0
NORMALIZED_VARIANT_WEIGHT = 0.35
ESCAPED_VARIANT_WEIGHT = 0.30
DIRICHLET_ALPHA = 0.5
MIN_FEATURE_COUNT = 2.0
MIN_FEATURE_DOC_FREQ = 2
MIN_TOTAL_TRIGRAM_MASS = 512.0

# Optional NB-only holdout evaluation buckets. These evaluate only the trigram
# model, not the deterministic structural detectors used at runtime.
EVAL_GAP_THRESHOLD = 0.30
EVAL_CONFIDENCE_THRESHOLD = 0.85
LENGTH_BUCKETS = (
    (8, 15),
    (16, 31),
    (32, 63),
    (64, 127),
    (128, 255),
    (256, 511),
    (512, None),
)

WHITESPACE_RE = re.compile(r"\s+")
CONTROL_RE = re.compile(r"[\x00-\x08\x0B\x0C\x0E-\x1F]+")
HTML_TAG_RE = re.compile(r"</?[A-Za-z][^>]*>")
URL_RE = re.compile(r"^(?:https?|ftps?|file|mailto|ssh|git|wss?)://", re.I)
PATH_RE = re.compile(
    r"^(?:[A-Za-z]:[\\/]|/|\.{1,2}/)[^\s]+$"
)
SQL_LEADER_RE = re.compile(
    r"^\s*(?:select|insert|update|delete|create|alter|drop|merge|with|grant|revoke|begin|commit|rollback|explain)\b",
    re.I,
)
SQL_SUPPORT_RE = re.compile(
    r"\b(?:from|where|join|group\s+by|order\s+by|having|limit|values|into|set|table|index|union|distinct)\b",
    re.I,
)
JSON_LIKE_RE = re.compile(r'^\s*[\[{].*:\s*.+[\]}]\s*$', re.S)
CSS_LIKE_RE = re.compile(r"\{[^{}]*:[^{};]+;[^{}]*\}", re.S)
SHELL_SIGNAL_RE = re.compile(r"(?:\$\{?[A-Za-z_]|(?:\|\|?|&&)|(?:2?>)|\b(?:grep|sed|awk|curl|git|docker|make|cmake|find|xargs)\b)")

# Seed snippets remain useful for categories that are sparse even in extracted
# literals: short UI strings, paths, URLs, placeholders, and log-like messages.
PLAIN_TEXT_SEED_SNIPPETS = [
    "Save changes before closing?",
    "No results found",
    "Loading configuration from defaults",
    "Retry request in 30 seconds",
    "Operation completed successfully",
    "Unexpected end of input",
    "Permission denied",
    "Please enter your username and password",
    "Settings saved",
    "Could not connect to the server",
    "Version 2.4.1",
    "Untitled document",
    "user@example.com",
    "/usr/local/bin/python3",
    "../build/output.log",
    "C:\\Program Files\\LLVM\\bin\\clang.exe",
    "https://example.com/api/v1/users?id=42",
    "git checkout feature/parser",
    "--output={path}",
    "${workspaceFolder}/src/main.cpp",
    "name=%s count=%d",
    "warning: cache miss on primary lookup",
    "network timeout while waiting for response",
    "The selected item is no longer available",
    "Tap to retry",
    "bonjour le monde",
    "naive facade",
    "こんにちは",
]


def pack_trigram_bytes(a: int, b: int, c: int) -> int:
    """Pack three bytes into a uint32_t key."""
    return (a << 16) | (b << 8) | c


def sorted_walk(root: str) -> Iterator[tuple[str, list[str], list[str]]]:
    """Yield os.walk results in a stable, sorted order."""
    for current_root, dirs, files in os.walk(root):
        dirs.sort()
        files.sort()
        yield current_root, dirs, files


def iter_sorted_files(root: str) -> Iterator[str]:
    """Yield files under *root* using deterministic traversal."""
    if not os.path.isdir(root):
        return
    for current_root, dirs, files in sorted_walk(root):
        dirs[:] = [name for name in dirs if not name.startswith(".") and name != "filenames"]
        for name in files:
            if name.startswith("."):
                continue
            yield os.path.join(current_root, name)


def iter_sorted_dirs(root: str) -> Iterator[os.DirEntry]:
    """Yield scandir entries in deterministic order."""
    try:
        with os.scandir(root) as entries:
            for entry in sorted(entries, key=lambda e: e.name):
                if entry.name.startswith("."):
                    continue
                yield entry
    except OSError:
        return


def read_file_safe(path: str, max_bytes: int = MAX_FILE_BYTES) -> str | None:
    """Read a file as UTF-8, skipping binary or oversized files."""
    try:
        size = os.path.getsize(path)
        if size > max_bytes or size == 0:
            return None
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            content = handle.read()
        if "\x00" in content[:512]:
            return None
        return content
    except OSError:
        return None


def normalize_source(text: str) -> str:
    """Normalize line endings and control characters, preserving structure."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return CONTROL_RE.sub(" ", text)


def collapse_whitespace(text: str) -> str:
    """Collapse repeated whitespace while preserving punctuation."""
    return WHITESPACE_RE.sub(" ", text).strip()


def count_trigrams(text: str) -> Counter[int]:
    """Count UTF-8 byte trigrams in *text*."""
    data = text.encode("utf-8", errors="ignore")
    if len(data) < 3:
        return Counter()

    counts: Counter[int] = Counter()
    for i in range(len(data) - 2):
        counts[pack_trigram_bytes(data[i], data[i + 1], data[i + 2])] += 1
    return counts


def stable_fraction(key: str, seed: int) -> float:
    """Map a string to a deterministic float in [0, 1)."""
    digest = hashlib.blake2b(
        f"{seed}\0{key}".encode("utf-8", errors="ignore"),
        digest_size=8,
    ).digest()
    value = int.from_bytes(digest, byteorder="big", signed=False)
    return value / float(1 << 64)


def evenly_spaced_offsets(length: int, window: int, count: int) -> list[int]:
    """Return up to *count* deterministic offsets covering a span evenly."""
    if length <= window or count <= 1:
        return [0]

    span = length - window
    if span <= 0:
        return [0]

    offsets = {
        min(span, max(0, round(i * span / (count - 1)))) for i in range(count)
    }
    return sorted(offsets)


def take_evenly_spaced(items: Sequence[str], limit: int) -> list[str]:
    """Reduce a sequence to *limit* elements while preserving broad coverage."""
    if len(items) <= limit:
        return list(items)

    if limit <= 1:
        return [items[0]]

    result = []
    last_index = len(items) - 1
    for i in range(limit):
        index = round(i * last_index / (limit - 1))
        result.append(items[index])
    return result


def sample_length_windows(text: str) -> list[str]:
    """Generate deterministic windows that approximate embedded snippet lengths."""
    text = text.strip()
    if len(text) < MIN_SNIPPET_CHARS:
        return []

    windows: list[str] = []
    if len(text) <= MAX_SNIPPET_CHARS:
        windows.append(text)

    for target_len in TARGET_SNIPPET_LENGTHS:
        if len(text) <= target_len:
            windows.append(text)
            continue

        for start in evenly_spaced_offsets(
            len(text), target_len, MAX_WINDOWS_PER_UNIT
        ):
            fragment = text[start : start + target_len].strip()
            if fragment:
                windows.append(fragment)

    deduped = []
    seen = set()
    for window in windows:
        if window not in seen:
            seen.add(window)
            deduped.append(window)
    return deduped


def is_comment_or_boilerplate_line(line: str, language_name: str) -> bool:
    """Reject comment lines and low-signal file boilerplate."""
    stripped = line.strip()
    if not stripped:
        return True

    if stripped.startswith("#!"):
        return False

    comment_prefixes = ("//", "/*", "*", "*/", "<!--", "-- ", "#")
    if stripped.startswith(comment_prefixes):
        return True

    lower = stripped.lower()
    boilerplate_prefixes = (
        "import ",
        "from ",
        "package ",
        "namespace ",
        "using namespace ",
        "#include ",
        "#pragma ",
        "module ",
        "open ",
        "copyright ",
        "spdx-license-identifier:",
    )
    if lower.startswith(boilerplate_prefixes):
        return True

    if language_name in STRUCTURED_LANGUAGES:
        return False

    return False


def is_informative_snippet(text: str) -> bool:
    """Reject trivially short or low-signal snippets."""
    stripped = text.strip()
    if len(stripped) < MIN_SNIPPET_CHARS:
        return False
    if len(set(stripped)) < 3:
        return False
    if sum(ch.isalnum() for ch in stripped) < 3 and stripped.count("/") < 2:
        return False
    return True


def split_inline_fragments(line: str, language_name: str) -> list[str]:
    """Extract shorter line-local fragments that resemble embedded payloads."""
    stripped = line.strip()
    if len(stripped) < MIN_SNIPPET_CHARS:
        return []

    if language_name == "Shell":
        parts = re.split(r"\s*(?:\|\||&&|\||;)\s*", stripped)
    elif language_name in {"SQL", "C", "C++", "C#", "Java", "JavaScript", "TypeScript",
                           "Go", "Rust", "Ruby", "PHP", "Perl", "Lua", "Swift",
                           "Kotlin", "Scala", "Dart", "Objective-C", "GLSL", "HLSL"}:
        parts = re.split(r"\s*;\s*", stripped)
    else:
        parts = []

    return [part for part in parts if part and part != stripped]


def add_candidate_windows(target: dict[str, None], text: str) -> None:
    """Normalize a candidate unit and add derived windows into *target*."""
    normalized = normalize_source(text)
    if not normalized.strip():
        return

    normalized = textwrap.dedent(normalized).strip()
    if not normalized:
        return

    for window in sample_length_windows(normalized):
        if not is_informative_snippet(window):
            continue
        target.setdefault(window, None)
        if len(target) >= MAX_SNIPPETS_PER_FILE * 2:
            return


def extract_language_snippets(text: str, language_name: str) -> list[str]:
    """Extract deterministic embedded-like snippets from a source file."""
    text = normalize_source(text)
    snippets: dict[str, None] = {}

    raw_lines = [line.rstrip() for line in text.split("\n")]
    filtered_lines = [
        line
        for line in raw_lines
        if not is_comment_or_boilerplate_line(line, language_name)
    ]

    if filtered_lines:
        for line in filtered_lines:
            add_candidate_windows(snippets, line)
            for fragment in split_inline_fragments(line, language_name):
                add_candidate_windows(snippets, fragment)

        blocks: list[list[str]] = []
        current: list[str] = []
        for line in raw_lines:
            if not line.strip():
                if current:
                    blocks.append(current)
                    current = []
                continue
            if is_comment_or_boilerplate_line(line, language_name):
                continue
            current.append(line)
        if current:
            blocks.append(current)

        for block in blocks:
            block_text = "\n".join(block)
            add_candidate_windows(snippets, block_text)

            for window_size in WINDOW_LINE_COUNTS:
                if len(block) < window_size:
                    continue
                for start in evenly_spaced_offsets(
                    len(block), window_size, MAX_WINDOWS_PER_UNIT
                ):
                    add_candidate_windows(
                        snippets, "\n".join(block[start : start + window_size])
                    )

            if len(snippets) >= MAX_SNIPPETS_PER_FILE * 2:
                break

    # Whole-file fallback still uses deterministic length buckets.
    add_candidate_windows(snippets, text)

    ordered = list(snippets.keys())
    return take_evenly_spaced(ordered, MAX_SNIPPETS_PER_FILE)


def escape_c_style(text: str) -> str:
    """Encode a snippet as the source-form body of a C-like string literal."""
    escaped: list[str] = []
    for ch in text:
        if ch == "\\":
            escaped.append("\\\\")
        elif ch == "\n":
            escaped.append("\\n")
        elif ch == "\r":
            escaped.append("\\r")
        elif ch == "\t":
            escaped.append("\\t")
        elif ch == '"':
            escaped.append('\\"')
        elif ch == "'":
            escaped.append("\\'")
        elif ord(ch) < 0x20:
            escaped.append(f"\\x{ord(ch):02x}")
        else:
            escaped.append(ch)
    return "".join(escaped)


def unescape_c_style(text: str) -> str:
    """Decode the common escape sequences seen in source-form string bodies."""
    result: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != "\\" or i + 1 >= len(text):
            result.append(ch)
            i += 1
            continue

        nxt = text[i + 1]
        simple = {
            "\\": "\\",
            "'": "'",
            '"': '"',
            "a": "\a",
            "b": "\b",
            "f": "\f",
            "n": "\n",
            "r": "\r",
            "t": "\t",
            "v": "\v",
            "0": "\0",
        }
        if nxt in simple:
            result.append(simple[nxt])
            i += 2
            continue

        if nxt == "x":
            hex_digits = []
            j = i + 2
            while j < len(text) and len(hex_digits) < 2 and text[j] in "0123456789abcdefABCDEF":
                hex_digits.append(text[j])
                j += 1
            if hex_digits:
                result.append(chr(int("".join(hex_digits), 16)))
                i = j
                continue

        if nxt in {"u", "U"}:
            width = 4 if nxt == "u" else 8
            start = i + 2
            digits = text[start : start + width]
            if len(digits) == width and all(
                c in "0123456789abcdefABCDEF" for c in digits
            ):
                try:
                    result.append(chr(int(digits, 16)))
                    i = start + width
                    continue
                except ValueError:
                    pass

        result.append(nxt)
        i += 2

    return "".join(result)


def try_parse_raw_string(text: str, start: int) -> tuple[str, int] | None:
    """Parse a C++ raw string beginning at *start* if present."""
    if text[start] != "R" or start + 1 >= len(text) or text[start + 1] != '"':
        return None

    open_paren = text.find("(", start + 2, start + 20)
    if open_paren == -1:
        return None

    delim = text[start + 2 : open_paren]
    if any(ch.isspace() or ch in {"(", ")", "\\"} for ch in delim):
        return None

    close_token = ")" + delim + '"'
    close = text.find(close_token, open_paren + 1)
    if close == -1:
        return None

    return text[open_paren + 1 : close], close + len(close_token)


def extract_string_literals(text: str) -> list[str]:
    """Extract generic quoted string bodies from source text."""
    literals: list[str] = []
    i = 0
    while i < len(text):
        raw = try_parse_raw_string(text, i)
        if raw is not None:
            body, end = raw
            literals.append(body)
            i = end
            continue

        quote = text[i]
        if quote not in {'"', "'", "`"}:
            i += 1
            continue

        if (
            quote == "'"
            and i > 0
            and i + 1 < len(text)
            and text[i - 1].isalnum()
            and text[i + 1].isalnum()
        ):
            i += 1
            continue

        triple = text.startswith(quote * 3, i)
        start = i + (3 if triple else 1)
        j = start

        while j < len(text):
            if triple:
                if text.startswith(quote * 3, j):
                    body = text[start:j]
                    literals.append(body if quote == "`" else unescape_c_style(body))
                    i = j + 3
                    break
                j += 1
                continue

            ch = text[j]
            if ch == "\\":
                j += 2
                continue
            if ch == quote:
                body = text[start:j]
                literals.append(body if quote == "`" else unescape_c_style(body))
                i = j + 1
                break
            if quote != "`" and ch in "\n\r":
                i += 1
                break
            j += 1
        else:
            i += 1

    return literals


def looks_like_structured_payload(text: str) -> bool:
    """Heuristically reject non-PlainText embedded payloads."""
    stripped = text.strip()
    if not stripped:
        return True

    if URL_RE.match(stripped):
        return False
    if PATH_RE.match(stripped):
        return False

    if JSON_LIKE_RE.match(stripped):
        return True
    if HTML_TAG_RE.search(stripped):
        return True
    if CSS_LIKE_RE.search(stripped):
        return True
    if SQL_LEADER_RE.match(stripped) and SQL_SUPPORT_RE.search(stripped):
        return True
    if SHELL_SIGNAL_RE.search(stripped) and ("|" in stripped or "$" in stripped or ">" in stripped):
        return True

    punctuation = sum(ch in "{}[]<>;:=|$" for ch in stripped)
    if punctuation / max(len(stripped), 1) > 0.35 and any(
        token in stripped for token in ("{", "[", "<", ";", "|", "$")
    ):
        return True

    return False


def is_plain_text_candidate(text: str) -> bool:
    """Keep likely prose, labels, paths, URLs, logs, and ordinary messages."""
    text = collapse_whitespace(normalize_source(text))
    if len(text) < 4:
        return False
    if not (
        any(ch.isalpha() for ch in text)
        or URL_RE.match(text)
        or PATH_RE.match(text)
        or any(ch in text for ch in "/._:-%{}")
    ):
        return False
    if looks_like_structured_payload(text):
        return False
    return True


def extract_plain_text_snippets(text: str) -> list[str]:
    """Extract prose-like snippets from README/text content."""
    text = normalize_source(text)
    snippets: dict[str, None] = {}

    paragraphs = re.split(r"\n\s*\n+", text)
    for paragraph in paragraphs:
        normalized = collapse_whitespace(paragraph)
        if is_plain_text_candidate(normalized):
            add_candidate_windows(snippets, normalized)

    for line in text.splitlines():
        normalized = collapse_whitespace(line)
        if is_plain_text_candidate(normalized):
            add_candidate_windows(snippets, normalized)

    return take_evenly_spaced(list(snippets.keys()), MAX_SNIPPETS_PER_FILE)


def make_training_variants(snippet: str) -> list[tuple[str, float]]:
    """Create raw, normalized, and escaped variants for training."""
    base = normalize_source(snippet).strip()
    if not is_informative_snippet(base):
        return []

    variants: list[tuple[str, float]] = [(base, 1.0)]

    normalized = collapse_whitespace(base)
    if normalized and normalized != base and is_informative_snippet(normalized):
        variants.append((normalized, NORMALIZED_VARIANT_WEIGHT))

    escaped = escape_c_style(base)
    if escaped != base and is_informative_snippet(escaped):
        variants.append((escaped, ESCAPED_VARIANT_WEIGHT))

        escaped_normalized = collapse_whitespace(escaped)
        if (
            escaped_normalized
            and escaped_normalized != escaped
            and is_informative_snippet(escaped_normalized)
        ):
            variants.append(
                (escaped_normalized, ESCAPED_VARIANT_WEIGHT * NORMALIZED_VARIANT_WEIGHT)
            )

    deduped: list[tuple[str, float]] = []
    seen = set()
    for text_value, weight in variants:
        if text_value not in seen:
            seen.add(text_value)
            deduped.append((text_value, weight))
    return deduped


def append_eval_samples(
    existing: list[str], snippets: Iterable[str], cap: int = MAX_EVAL_SNIPPETS_PER_CLASS
) -> list[str]:
    """Append and cap evaluation snippets while keeping broad file coverage."""
    combined = existing + list(snippets)
    if len(combined) <= cap:
        return combined
    return take_evenly_spaced(combined, cap)


def accumulate_training_counts(snippets: Sequence[str]) -> tuple[Counter[int], Counter[int]]:
    """Convert a file's snippets into normalized trigram counts and doc frequency."""
    file_counts: Counter[int] = Counter()

    for snippet in snippets:
        for variant, weight in make_training_variants(snippet):
            tri_counts = count_trigrams(variant)
            if not tri_counts:
                continue
            for tri, count in tri_counts.items():
                file_counts[tri] += count * weight

    if not file_counts:
        return Counter(), Counter()

    scale = PER_FILE_TRIGRAM_MASS / sum(file_counts.values())
    scaled_counts: Counter[int] = Counter()
    doc_freq: Counter[int] = Counter()
    for tri, count in file_counts.items():
        scaled_counts[tri] = count * scale
        doc_freq[tri] = 1

    return scaled_counts, doc_freq


def process_language(
    samples_dir: str,
    linguist_name: str,
    seed: int,
    eval_holdout: float,
) -> tuple[Counter[int], Counter[int], float, int, int, list[str]]:
    """Read sample files for one language and return training counts."""
    lang_dir = os.path.join(samples_dir, linguist_name)
    if not os.path.isdir(lang_dir):
        return Counter(), Counter(), 0.0, 0, 0, []

    combined_counts: Counter[int] = Counter()
    combined_doc_freq: Counter[int] = Counter()
    total_mass = 0.0
    file_count = 0
    snippet_count = 0
    eval_snippets: list[str] = []

    for path in iter_sorted_files(lang_dir):
        content = read_file_safe(path)
        if content is None:
            continue

        snippets = extract_language_snippets(content, linguist_name)
        if not snippets:
            continue

        if eval_holdout > 0.0 and stable_fraction(path, seed) < eval_holdout:
            eval_snippets = append_eval_samples(
                eval_snippets,
                take_evenly_spaced(snippets, min(24, len(snippets))),
            )
            continue

        file_counts, file_doc_freq = accumulate_training_counts(snippets)
        if not file_counts:
            continue

        combined_counts.update(file_counts)
        combined_doc_freq.update(file_doc_freq)
        total_mass += PER_FILE_TRIGRAM_MASS
        file_count += 1
        snippet_count += len(snippets)

    return (
        combined_counts,
        combined_doc_freq,
        total_mass,
        file_count,
        snippet_count,
        eval_snippets,
    )


def build_plain_text_corpus(
    samples_dir: str | None,
    seed: int,
    eval_holdout: float,
) -> tuple[Counter[int], Counter[int], float, int, int, list[str]]:
    """Build the PlainText class from real literals and text-oriented corpora."""
    combined_counts: Counter[int] = Counter()
    combined_doc_freq: Counter[int] = Counter()
    total_mass = 0.0
    file_count = 0
    snippet_count = 0
    eval_snippets: list[str] = []

    def add_training_file(key: str, snippets: Sequence[str]) -> None:
        nonlocal total_mass, file_count, snippet_count, eval_snippets
        if not snippets:
            return

        if eval_holdout > 0.0 and stable_fraction(key, seed) < eval_holdout:
            eval_snippets = append_eval_samples(
                eval_snippets,
                take_evenly_spaced(list(snippets), min(24, len(snippets))),
            )
            return

        file_counts, file_doc_freq = accumulate_training_counts(snippets)
        if not file_counts:
            return

        combined_counts.update(file_counts)
        combined_doc_freq.update(file_doc_freq)
        total_mass += PER_FILE_TRIGRAM_MASS
        file_count += 1
        snippet_count += len(snippets)

    add_training_file("plain_text_seed", PLAIN_TEXT_SEED_SNIPPETS)

    if not samples_dir or not os.path.isdir(samples_dir):
        return (
            combined_counts,
            combined_doc_freq,
            total_mass,
            file_count,
            snippet_count,
            eval_snippets,
        )

    for dirname in TEXT_LANGUAGE_DIRS:
        text_dir = os.path.join(samples_dir, dirname)
        for path in iter_sorted_files(text_dir):
            content = read_file_safe(path)
            if content is None:
                continue
            snippets = extract_plain_text_snippets(content)
            add_training_file(path, snippets)

    for entry in iter_sorted_dirs(samples_dir):
        if not entry.is_dir():
            continue
        for name in sorted(os.listdir(entry.path)):
            lower = name.lower()
            if not (lower.startswith("readme") or lower.endswith(".txt")):
                continue
            path = os.path.join(entry.path, name)
            content = read_file_safe(path)
            if content is None:
                continue
            snippets = extract_plain_text_snippets(content)
            add_training_file(path, snippets)

    for linguist_name in sorted(LANGUAGE_MAP):
        lang_dir = os.path.join(samples_dir, linguist_name)
        if not os.path.isdir(lang_dir):
            continue
        for path in iter_sorted_files(lang_dir):
            content = read_file_safe(path)
            if content is None:
                continue
            literals = [
                collapse_whitespace(snippet)
                for snippet in extract_string_literals(content)
            ]
            plain_literals = [
                literal for literal in literals if is_plain_text_candidate(literal)
            ]
            add_training_file(f"literal::{path}", take_evenly_spaced(plain_literals, 32))

    return (
        combined_counts,
        combined_doc_freq,
        total_mass,
        file_count,
        snippet_count,
        eval_snippets,
    )


def select_discriminative_trigrams(
    counts: Counter[int],
    doc_freq: Counter[int],
    total_mass: float,
    all_counts: Counter[int],
    all_total_mass: float,
    file_count: int,
) -> list[int]:
    """Keep the trigrams that best separate one class from the rest."""
    if total_mass <= 0.0:
        return []

    other_total_mass = max(all_total_mass - total_mass, 1.0)
    vocab = max(len(all_counts), 1)
    min_doc_freq = MIN_FEATURE_DOC_FREQ if file_count >= 4 else 1
    min_count = MIN_FEATURE_COUNT if file_count >= 4 else 1.0

    scored: list[tuple[float, float, int]] = []
    for tri, count in counts.items():
        if count < min_count:
            continue
        if doc_freq.get(tri, 0) < min_doc_freq:
            continue

        other = max(all_counts.get(tri, 0.0) - count, 0.0)
        log_odds = math.log((count + DIRICHLET_ALPHA) / (total_mass + DIRICHLET_ALPHA * vocab))
        log_odds -= math.log(
            (other + DIRICHLET_ALPHA) / (other_total_mass + DIRICHLET_ALPHA * vocab)
        )
        if log_odds <= 0.0:
            continue

        support = math.log1p(count) * math.log1p(doc_freq.get(tri, 1))
        score = log_odds * support
        scored.append((score, count, tri))

    if not scored:
        return [tri for tri, _ in counts.most_common(TOP_N_TRIGRAMS)]

    top = heapq.nlargest(
        TOP_N_TRIGRAMS,
        scored,
        key=lambda item: (item[0], item[1], -item[2]),
    )
    return [tri for _, _, tri in top]


def compute_model(
    counts: Counter[int],
    selected_trigrams: Sequence[int],
) -> tuple[dict[int, float], float] | None:
    """Compute adjusted trigram log-probabilities for one language model.

    The generated table stores log-probability boosts relative to the unseen
    baseline:

      adjusted = log((count + 1) / denom) - log(1 / denom) = log(count + 1)

    This matches the runtime scoring in LanguageClassifier.cpp:

      score = logPrior + N * unseenLogProb + sum(adjusted boosts)
    """
    if not selected_trigrams:
        return None

    total = sum(counts[tri] for tri in selected_trigrams)
    if total < MIN_TOTAL_TRIGRAM_MASS:
        return None

    vocab = len(selected_trigrams)
    denom = total + vocab + 1.0  # +1 for the unseen class
    unseen_log_prob = math.log(1.0 / denom)

    trigram_probs = {}
    for tri in selected_trigrams:
        trigram_probs[tri] = math.log(counts[tri] + 1.0)

    return trigram_probs, unseen_log_prob


def _process_one_language(
    args_tuple: tuple[str, str, str, int, int, float],
) -> tuple[
    str,
    str,
    int,
    Counter[int],
    Counter[int],
    float,
    int,
    int,
    list[str],
]:
    """Worker function for parallel language processing."""
    samples_dir, linguist_name, enum_name, lang_id, seed, eval_holdout = args_tuple
    counts, doc_freq, total_mass, file_count, snippet_count, eval_snippets = (
        process_language(samples_dir, linguist_name, seed, eval_holdout)
    )
    return (
        linguist_name,
        enum_name,
        lang_id,
        counts,
        doc_freq,
        total_mass,
        file_count,
        snippet_count,
        eval_snippets,
    )


def iter_language_results(
    tasks: Sequence[tuple[str, str, str, int, int, float]]
) -> Iterator[
    tuple[
        str,
        str,
        int,
        Counter[int],
        Counter[int],
        float,
        int,
        int,
        list[str],
    ]
]:
    """Run language processing in parallel when possible, with a sequential fallback."""
    try:
        with ProcessPoolExecutor() as pool:
            futures = {pool.submit(_process_one_language, task): task for task in tasks}
            for future in as_completed(futures):
                yield future.result()
        return
    except (OSError, PermissionError) as exc:
        print(f"  Worker pool unavailable ({exc}); falling back to sequential processing")

    for task in tasks:
        yield _process_one_language(task)


def build_eval_index(
    models: dict[str, tuple[int, dict[int, float], float]]
) -> tuple[dict[int, list[tuple[str, float]]], dict[str, tuple[float, float]]]:
    """Prepare a compact index for Python-side holdout evaluation."""
    trigram_index: dict[int, list[tuple[str, float]]] = {}
    language_meta: dict[str, tuple[float, float]] = {}

    for enum_name, (_, trigrams, unseen) in models.items():
        language_meta[enum_name] = (math.log(1.0 / len(models)), unseen)
        for tri, adjusted in trigrams.items():
            trigram_index.setdefault(tri, []).append((enum_name, adjusted))

    return trigram_index, language_meta


def classify_snippet_nb(
    text: str,
    trigram_index: dict[int, list[tuple[str, float]]],
    language_meta: dict[str, tuple[float, float]],
) -> tuple[str | None, str | None]:
    """Classify a snippet using the generated NB tables only.

    Returns:
      (raw_best_label, accepted_label_or_None)
    """
    trigram_counts = count_trigrams(text)
    num_trigrams = sum(trigram_counts.values())
    if num_trigrams == 0:
        return None, None

    scores = {
        label: meta[0] + num_trigrams * meta[1]
        for label, meta in language_meta.items()
    }

    for tri, count in trigram_counts.items():
        for label, adjusted in trigram_index.get(tri, ()):
            scores[label] += adjusted * count

    sorted_scores = sorted(scores.items(), key=lambda item: item[1], reverse=True)
    best_label, max_score = sorted_scores[0]
    second_score = (
        sorted_scores[1][1]
        if len(sorted_scores) > 1
        else -math.inf
    )

    sum_exp = sum(math.exp(score - max_score) for _, score in sorted_scores)
    confidence = 1.0 / sum_exp if sum_exp > 0.0 else 0.0
    gap = (max_score - second_score) / num_trigrams if num_trigrams > 0 else 0.0

    accepted = best_label if (
        confidence >= EVAL_CONFIDENCE_THRESHOLD and gap >= EVAL_GAP_THRESHOLD
    ) else None
    return best_label, accepted


def length_bucket_name(length: int) -> str:
    """Format a snippet length bucket label."""
    for lower, upper in LENGTH_BUCKETS:
        if upper is None:
            if length >= lower:
                return f"{lower}+"
            continue
        if lower <= length <= upper:
            return f"{lower}-{upper}"
    return "<8"


def evaluate_models(
    eval_samples: dict[str, list[str]],
    models: dict[str, tuple[int, dict[int, float], float]],
) -> None:
    """Run an NB-only holdout evaluation by snippet length bucket."""
    trigram_index, language_meta = build_eval_index(models)
    stats: dict[str, Counter[str]] = {}

    for label, snippets in sorted(eval_samples.items()):
        for snippet in snippets:
            bucket = length_bucket_name(len(snippet))
            bucket_stats = stats.setdefault(bucket, Counter())
            bucket_stats["total"] += 1

            raw_best, accepted = classify_snippet_nb(
                snippet, trigram_index, language_meta
            )
            if raw_best == label:
                bucket_stats["top1"] += 1
            if accepted is None:
                bucket_stats["rejected"] += 1
                continue
            bucket_stats["accepted"] += 1
            if accepted == label:
                bucket_stats["accepted_correct"] += 1

    if not stats:
        print("\nHoldout evaluation: no snippets collected")
        return

    print("\nHoldout evaluation (Naive Bayes only):")
    ordered_buckets = [length_bucket_name(low) for low, _ in LENGTH_BUCKETS]
    seen = set()
    for bucket in ordered_buckets + sorted(stats):
        if bucket not in stats or bucket in seen:
            continue
        seen.add(bucket)
        bucket_stats = stats[bucket]
        total = max(bucket_stats["total"], 1)
        accepted = bucket_stats["accepted"]
        accepted_acc = (
            100.0 * bucket_stats["accepted_correct"] / accepted if accepted else 0.0
        )
        print(
            "  "
            f"{bucket}: "
            f"top1={100.0 * bucket_stats['top1'] / total:5.1f}% "
            f"accepted={100.0 * accepted / total:5.1f}% "
            f"accepted_acc={accepted_acc:5.1f}% "
            f"n={bucket_stats['total']}"
        )


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------


def write_header(
    models: dict[str, tuple[int, dict[int, float], float]],
    output_path: str,
) -> None:
    """Write the C++ generated header with all model data."""
    lang_index = {name: i for i, name in enumerate(sorted(models))}

    combined = []
    for enum_name, (_, trigrams, _) in models.items():
        idx = lang_index[enum_name]
        for tri, adjusted in trigrams.items():
            combined.append((tri, idx, adjusted))

    combined.sort(key=lambda entry: (entry[0], entry[1]))

    num_langs = len(models)
    log_prior = math.log(1.0 / num_langs) if num_langs > 0 else 0.0

    with open(output_path, "w", encoding="utf-8") as handle:
        handle.write(
            "//\n"
            "// LanguageModel.generated.hpp\n"
            "// Auto-generated by train/generate_model.py — DO NOT EDIT MANUALLY\n"
            "//\n"
            "// Regenerate with:\n"
            "//   python3 train/generate_model.py <linguist-samples-dir>"
            " -o include/LanguageModel.generated.hpp\n"
            "//\n\n"
            "#ifndef LANGUAGE_MODEL_GENERATED_HPP\n"
            "#define LANGUAGE_MODEL_GENERATED_HPP\n\n"
            "#include <cstddef>\n"
            "#include <cstdint>\n\n"
            "/// Packed trigram entry: trigram = (b0 << 16) | (b1 << 8) | b2.\n"
            "/// Entries within the combined table are sorted by "
            "(trigram, languageIdx).\n"
            "struct TrigramEntry {\n"
            "  uint32_t trigram;\n"
            "  uint8_t languageIdx;\n"
            "  float adjustedLogProb;\n"
            "};\n\n"
            "/// Per-language metadata used by the Naive Bayes classifier.\n"
            "struct LanguageInfo {\n"
            "  const char *name;\n"
            "  uint8_t languageId;\n"
            "  float logPrior;\n"
            "  float unseenLogProb;\n"
            "};\n\n"
        )

        handle.write(f"static constexpr size_t kNumLanguages = {num_langs};\n\n")
        handle.write("static constexpr LanguageInfo kLanguageInfos[] = {\n")
        for enum_name in sorted(models):
            lang_id, _, unseen = models[enum_name]
            handle.write(
                f'    {{"{enum_name}", {lang_id}, '
                f"{log_prior:.8f}f, {unseen:.8f}f}},\n"
            )
        handle.write("};\n\n")

        handle.write(
            f"static constexpr size_t kNumCombinedEntries = {len(combined)};\n\n"
        )
        handle.write("static constexpr TrigramEntry kCombinedEntries[] = {\n")
        for tri, idx, adjusted in combined:
            handle.write(f"    {{0x{tri:06X}u, {idx}, {adjusted:.6f}f}},\n")
        handle.write("};\n\n")
        handle.write("#endif // LANGUAGE_MODEL_GENERATED_HPP\n")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def _set_top_n(n: int) -> None:
    global TOP_N_TRIGRAMS
    TOP_N_TRIGRAMS = n


def main() -> None:
    """Parse CLI options, build corpora, emit the header, and optionally eval."""
    parser = argparse.ArgumentParser(
        description="Generate trigram language model from Linguist samples."
    )
    parser.add_argument("samples_dir", help="Path to linguist/samples/ directory")
    parser.add_argument(
        "-o",
        "--output",
        default="include/LanguageModel.generated.hpp",
        help="Output header path (default: include/LanguageModel.generated.hpp)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Deterministic seed for holdout/evaluation splitting (default: 42)",
    )
    parser.add_argument(
        "--top-n",
        type=int,
        default=TOP_N_TRIGRAMS,
        help=f"Trigrams to keep per language (default: {TOP_N_TRIGRAMS})",
    )
    parser.add_argument(
        "--eval-holdout",
        type=float,
        default=0.0,
        help="Hold out this fraction of files per class for evaluation (default: 0.0)",
    )
    args = parser.parse_args()

    if not 0.0 <= args.eval_holdout < 1.0:
        print("Error: --eval-holdout must be in [0.0, 1.0)", file=sys.stderr)
        sys.exit(1)

    _set_top_n(args.top_n)

    if not os.path.isdir(args.samples_dir):
        print(f"Error: {args.samples_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    corpora: dict[
        str, tuple[int, Counter[int], Counter[int], float, int, list[str]]
    ] = {}
    models: dict[str, tuple[int, dict[int, float], float]] = {}

    print("Processing languages:")

    pt_counts, pt_doc_freq, pt_mass, pt_files, pt_snippets, pt_eval = build_plain_text_corpus(
        args.samples_dir,
        args.seed,
        args.eval_holdout,
    )
    if pt_mass > 0.0:
        corpora["PlainText"] = (
            1,
            pt_counts,
            pt_doc_freq,
            pt_mass,
            pt_files,
            pt_eval,
        )
        print(
            "  "
            f"PlainText: {pt_files} files, {pt_snippets} snippets, "
            f"mass={pt_mass:.0f}"
        )

    tasks = [
        (args.samples_dir, ling_name, enum_name, lang_id, args.seed, args.eval_holdout)
        for ling_name, (enum_name, lang_id) in sorted(LANGUAGE_MAP.items())
    ]

    for (
        ling_name,
        enum_name,
        lang_id,
        counts,
        doc_freq,
        total_mass,
        file_count,
        snippet_count,
        eval_snippets,
    ) in iter_language_results(tasks):
        if total_mass <= 0.0:
            print(f"  {ling_name}: skipped (no usable snippets)")
            continue

        corpora[enum_name] = (
            lang_id,
            counts,
            doc_freq,
            total_mass,
            file_count,
            eval_snippets,
        )
        print(
            "  "
            f"{ling_name}: {file_count} files, {snippet_count} snippets, "
            f"mass={total_mass:.0f}"
        )

    if not corpora:
        print("Error: no language corpora produced", file=sys.stderr)
        sys.exit(1)

    all_counts: Counter[int] = Counter()
    all_total_mass = 0.0
    for _, counts, _, total_mass, _, _ in corpora.values():
        all_counts.update(counts)
        all_total_mass += total_mass

    print("\nSelecting discriminative trigrams:")
    for enum_name in sorted(corpora):
        lang_id, counts, doc_freq, total_mass, file_count, _ = corpora[enum_name]
        selected = select_discriminative_trigrams(
            counts,
            doc_freq,
            total_mass,
            all_counts,
            all_total_mass,
            file_count,
        )
        result = compute_model(counts, selected)
        if result is None:
            print(f"  {enum_name}: skipped (insufficient discriminative data)")
            continue
        trigrams, unseen = result
        models[enum_name] = (lang_id, trigrams, unseen)
        print(f"  {enum_name}: {len(trigrams)} trigrams")

    if not models:
        print("Error: no language models produced", file=sys.stderr)
        sys.exit(1)

    total_entries = sum(len(trigrams) for _, trigrams, _ in models.values())
    print(f"\nTotal: {len(models)} languages, {total_entries} combined entries")

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_header(models, str(output_path))
    print(f"Generated: {output_path}")

    if args.eval_holdout > 0.0:
        eval_samples = {
            enum_name: corpora[enum_name][5]
            for enum_name in models
            if corpora[enum_name][5]
        }
        evaluate_models(eval_samples, models)


if __name__ == "__main__":
    main()
