.PHONY: usenix apply patch clean

SRC = ../doc
BUILD = build
PATCH = paper.patch
FILE = usenix

usenix: apply
	@latexmk -pdf -outdir=$(BUILD) main.tex
	@cp $(BUILD)/main.pdf $(FILE).pdf
	@rm -f main.pdf

apply: patch
	@cp $(SRC)/*.tex $(SRC)/refs.bib .
	@rm -rf generated && cp -R $(SRC)/generated .
	@for p in *.patch; do [ -s "$$p" ] || continue; patch -p1 < "$$p" || exit 1; done

# Regenerates $(PATCH) from the staged working copy; skipped on a fresh
# checkout so the committed patch is not wiped before the first stage.
patch:
	@[ -f main.tex ] || { echo "no staged sources, keeping $(PATCH)"; exit 0; }
	@rm -f $(PATCH)
	@{ (cd $(SRC) && ls *.tex refs.bib generated/*.tex); ls *.tex refs.bib generated/*.tex; } 2>/dev/null | sort -u | \
	while read f; do \
		a=$(SRC)/$$f; b=$$f; \
		[ -f "$$a" ] || a=/dev/null; \
		[ -f "$$b" ] || b=/dev/null; \
		git diff --no-index "$$a" "$$b" | \
		sed -e 's|^--- a/\.\./doc/|--- a/|' -e 's|^+++ b/\.\./doc/|+++ b/|' >> $(PATCH); \
	done; \
	[ -s $(PATCH) ] || rm -f $(PATCH)

clean:
	@rm -rf build/*

open:
	open $(FILE).pdf