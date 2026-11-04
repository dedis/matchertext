.PHONY: FORCE all fast split link clean open

FILE=main
BUILD=build

all: $(FILE).pdf

$(FILE).pdf: $(FILE).tex FORCE
	latexmk -pdf -outdir=$(BUILD) $(FILE).tex
	cp $(BUILD)/$(FILE).pdf $@

fast:
	mkdir -p $(BUILD)
	pdflatex -output-directory=$(BUILD) $(FILE)
	cp $(BUILD)/$(FILE).pdf $(FILE).pdf

# Split into $(FILE).pdf (body) and appendices.pdf at the page of appendix A.
split: $(FILE).pdf
	p=$$(sed -n 's/.*}{\([0-9]*\)}{appendix\.A}.*/\1/p' $(BUILD)/$(FILE).aux | head -n 1); \
	test -n "$$p" || { echo "split: appendix A not found" >&2; exit 1; }; \
	gs -q -dBATCH -dNOPAUSE -dSAFER -sDEVICE=pdfwrite -dLastPage=$$((p - 1)) \
		-sOutputFile=$(FILE).pdf $(BUILD)/$(FILE).pdf && \
	gs -q -dBATCH -dNOPAUSE -dSAFER -sDEVICE=pdfwrite -dFirstPage=$$p \
		-sOutputFile=appendices.pdf $(BUILD)/$(FILE).pdf

link:
	rm -f *.bib
	for lib in comp fault net os priv sec soc theory; do \
		ln -s ../$$lib.bib $$lib.bib ; \
	done

clean:
	$(RM) -r $(BUILD)
	$(RM) *.aux *.bak *~

open:
	open $(FILE).pdf
