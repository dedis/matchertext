.PHONY: FORCE lenny all clean distclean

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

link:
	rm -f *.bib
	for lib in comp fault net os priv sec soc theory; do \
		ln -s ../$$lib.bib $$lib.bib ; \
	done

clean:
	$(RM) -r $(BUILD)
	$(RM) *.aux *.bak *~

distclean: clean
	$(RM) $(FILE).pdf
	$(RM) $(FILE).ps
	$(RM) $(FILE).dvi
	$(RM) *.d
