.PHONY: FORCE lenny all clean distclean

FILE=main

all: $(FILE).pdf

%.pdf: %.tex FORCE
	latexmk -pdf $<

fast:
	pdflatex $(FILE)

link:
	rm -f *.bib
	for lib in comp fault net os priv sec soc theory; do \
		ln -s ../$$lib.bib $$lib.bib ; \
	done

clean:
	for ext in aux log toc lof lot lol dlog bbl \
		nav snm fls vrb blg out tpt fdb_latexmk; \
	do \
		$(RM) $(FILE).$$ext ; \
	done
	$(RM) *.aux *.bak *~

distclean: clean
	$(RM) $(FILE).pdf
	$(RM) $(FILE).ps
	$(RM) $(FILE).dvi
	$(RM) *.d
