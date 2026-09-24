#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

cd "$repo_root"
sha256sum -c docs/artifact/SHA256SUMS
unzip -tq docs/paper/paper-source.zip >/dev/null
unzip -q docs/paper/paper-source.zip -d "$work_dir/source"
[[ -s docs/paper/arxiv-submission.tar.gz ]] || {
  echo 'arXiv bundle is missing; run make artifact-arxiv' >&2
  exit 1
}
mkdir -p "$work_dir/arxiv"
tar -xzf docs/paper/arxiv-submission.tar.gz -C "$work_dir/arxiv"

for file in paper.tex references.bib; do
  if [[ ! -f "docs/paper/$file" || ! -f "$work_dir/source/$file" ]]; then
    echo "missing paper source: $file" >&2
    exit 1
  fi
  cmp "docs/paper/$file" "$work_dir/source/$file"
done
for path in docs/paper/figures/*.tex; do
  file="${path#docs/paper/}"
  [[ -f "$work_dir/source/$file" ]] || {
    echo "source archive is missing $file" >&2
    exit 1
  }
  cmp "$path" "$work_dir/source/$file"
done
[[ -s "$work_dir/source/paper.bbl" ]] || {
  echo 'source archive is missing paper.bbl' >&2
  exit 1
}
cmp "$work_dir/source/paper.bbl" "$work_dir/arxiv/paper.bbl"
cmp "$work_dir/source/references.bib" "$work_dir/arxiv/references.bib"
cmp <(sed 's/\\bibliography{references}/\\input{paper.bbl}/' \
  "$work_dir/source/paper.tex") "$work_dir/arxiv/paper.tex"
for path in docs/paper/figures/*.tex; do
  file="${path#docs/paper/}"
  cmp "$path" "$work_dir/arxiv/$file"
done
if grep -Fq '\bibliography{references}' "$work_dir/arxiv/paper.tex"; then
  echo 'arXiv bundle still requires BibTeX' >&2
  exit 1
fi

(
  cd "$work_dir/source"
  pdflatex -halt-on-error -interaction=batchmode paper.tex >/dev/null || {
    tail -50 paper.log >&2
    exit 1
  }
  bibtex paper >/dev/null
  pdflatex -halt-on-error -interaction=batchmode paper.tex >/dev/null
  pdflatex -halt-on-error -interaction=batchmode paper.tex >/dev/null
  [[ -s paper.pdf ]]
  if grep -Eq 'undefined citations|undefined references' paper.log; then
    echo 'isolated paper build has undefined citations or references' >&2
    exit 1
  fi
)

(
  cd "$work_dir/arxiv"
  pdflatex -halt-on-error -interaction=batchmode paper.tex >/dev/null
  pdflatex -halt-on-error -interaction=batchmode paper.tex >/dev/null
  [[ -s paper.pdf ]]
  if grep -Eq 'undefined citations|undefined references' paper.log; then
    echo 'isolated arXiv build has undefined citations or references' >&2
    exit 1
  fi
)

echo 'paper artifact: PASS'
