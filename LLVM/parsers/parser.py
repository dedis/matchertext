#!/usr/bin/env python3
#
# parser.py
# Python counterpart of Parser::ParseC_CPP.
# Emits a JSON array of {"kind": "string"|"comment", "value": "..."} on stdout.
# Usage: parser.py <path>

import io
import json
import re
import sys
import tokenize


_PREFIX_QUOTE = re.compile(r"^([rRbBuUfF]{0,3})('''|\"\"\"|'|\")")


def extract_string_body(spelling: str) -> str:
    m = _PREFIX_QUOTE.match(spelling)
    if not m:
        return spelling
    quote = m.group(2)
    body_start = m.end()
    body_end = spelling.rfind(quote)
    if body_end <= body_start - len(quote):
        return spelling[body_start:]
    return spelling[body_start:body_end]


def parse(path: str):
    items = []
    pending = None  # accumulator for adjacent string concatenation

    def flush_pending():
        nonlocal pending
        if pending is not None:
            items.append({"kind": "string", "value": pending})
            pending = None

    try:
        with open(path, "rb") as f:
            tokens = tokenize.tokenize(f.readline)
            for tok in tokens:
                ttype = tok.type
                if ttype == tokenize.STRING:
                    body = extract_string_body(tok.string)
                    pending = body if pending is None else pending + body
                elif ttype == tokenize.COMMENT:
                    flush_pending()
                    items.append({"kind": "comment", "value": tok.string})
                elif ttype in (
                    tokenize.NL,
                    tokenize.NEWLINE,
                    tokenize.INDENT,
                    tokenize.DEDENT,
                    tokenize.ENCODING,
                    tokenize.ENDMARKER,
                ):
                    continue
                else:
                    flush_pending()
            flush_pending()
    except (tokenize.TokenizeError, SyntaxError, OSError, UnicodeDecodeError):
        # Bail out gracefully — return what we collected so far.
        flush_pending()

    return items


def main() -> int:
    if len(sys.argv) < 2:
        sys.stdout.write("[]")
        return 0
    items = parse(sys.argv[1])
    json.dump(items, sys.stdout, ensure_ascii=False, separators=(",", ":"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
