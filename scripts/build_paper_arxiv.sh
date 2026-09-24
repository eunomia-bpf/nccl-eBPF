#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-$repo_root/docs/paper/arxiv-submission.tar.gz}"
if [[ "$output" != /* ]]; then
  output="$repo_root/$output"
fi
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

cd "$repo_root"
sha256sum -c docs/artifact/SHA256SUMS >/dev/null
unzip -q docs/paper/paper-source.zip -d "$work_dir"
[[ -s "$work_dir/paper.bbl" ]] || {
  echo 'paper-source.zip does not contain paper.bbl' >&2
  exit 1
}

# arXiv compiles the supplied bibliography directly. This edits only a
# temporary copy of the source archive, never the submitted paper source.
sed -i 's/\\bibliography{references}/\\input{paper.bbl}/' "$work_dir/paper.tex"
if grep -Fq '\bibliography{references}' "$work_dir/paper.tex"; then
  echo 'failed to replace bibliography command for arXiv' >&2
  exit 1
fi

mkdir -p "$(dirname "$output")"
tar -C "$work_dir" --sort=name --mtime="@${SOURCE_DATE_EPOCH:-0}" \
  --owner=0 --group=0 --numeric-owner -cf - . | gzip -n > "$output"
echo "arXiv bundle: $output"
