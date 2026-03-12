TEX      = paper
PDFLATEX = pdflatex -interaction=nonstopmode
BIBTEX   = bibtex

ARXIV_DIR  = _arxiv
ARXIV_TAR  = arxiv-submission.tar.gz

SRCS = $(TEX).tex references.bib $(wildcard figures/*.tex)

# ---- default: build PDF ----
all: $(TEX).pdf

$(TEX).pdf: $(SRCS)
	$(PDFLATEX) $(TEX)
	$(BIBTEX)   $(TEX)
	$(PDFLATEX) $(TEX)
	$(PDFLATEX) $(TEX)

# ---- arXiv bundle ----
arxiv: $(TEX).pdf $(ARXIV_TAR)
	@echo "=> $(ARXIV_TAR) ready for upload"

$(ARXIV_TAR): $(TEX).tex $(TEX).bbl $(wildcard figures/*.tex)
	rm -rf $(ARXIV_DIR)
	mkdir -p $(ARXIV_DIR)/figures
	cp $(TEX).tex $(TEX).bbl $(ARXIV_DIR)/
	cp figures/architecture.tex $(ARXIV_DIR)/figures/
	# arXiv: remove anonymous, use .bbl instead of .bib
	sed -i 's/\\documentclass\[sigconf,anonymous\]/\\documentclass[sigconf]/' $(ARXIV_DIR)/$(TEX).tex
	sed -i 's/\\bibliography{references}/\\input{$(TEX).bbl}/' $(ARXIV_DIR)/$(TEX).tex
	cd $(ARXIV_DIR) && tar czf ../$(ARXIV_TAR) .
	rm -rf $(ARXIV_DIR)

clean:
	rm -f $(TEX).{aux,bbl,blg,log,out,pdf,fls,fdb_latexmk,synctex.gz}
	rm -rf $(ARXIV_DIR) $(ARXIV_TAR)

.PHONY: all arxiv clean
