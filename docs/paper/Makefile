TEX      = paper
PDFLATEX = pdflatex -interaction=nonstopmode
BIBTEX   = bibtex

ARXIV_DIR  = _arxiv
ARXIV_TAR  = arxiv-submission.tar.gz
SOURCE_ZIP = paper-source.zip

SRCS = $(TEX).tex references.bib $(wildcard figures/*.tex)

# ---- default: build PDF ----
all: $(TEX).pdf

$(TEX).pdf: $(SRCS)
	$(PDFLATEX) $(TEX)
	$(BIBTEX)   $(TEX)
	$(PDFLATEX) $(TEX)
	$(PDFLATEX) $(TEX)

# ---- arXiv bundle ----
arxiv: $(ARXIV_TAR)
	@echo "=> $(ARXIV_TAR) ready for upload"

$(ARXIV_TAR): $(SOURCE_ZIP)
	../../scripts/build_paper_arxiv.sh

clean:
	rm -f $(TEX).{aux,bbl,blg,log,out,pdf,fls,fdb_latexmk,synctex.gz}
	rm -rf $(ARXIV_DIR) $(ARXIV_TAR)

.PHONY: all arxiv clean
