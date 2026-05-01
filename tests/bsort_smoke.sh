#!/bin/sh
set -eu

bsort_bin=$1
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

printf 'b2AAa2BBa1CCb1DD' > "$tmpdir/inplace"
printf 'a1CCa2BBb1DDb2AA' > "$tmpdir/expected"

"$bsort_bin" -r 4 -k 2 "$tmpdir/inplace" >/dev/null
cmp "$tmpdir/inplace" "$tmpdir/expected"

printf 'b2AAa2BBa1CCb1DD' > "$tmpdir/input"
"$bsort_bin" -r 4 -k 2 -i "$tmpdir/input" -o "$tmpdir/output" >/dev/null
cmp "$tmpdir/output" "$tmpdir/expected"

printf 'b2AAa2BBa1CCb1DD' > "$tmpdir/input-positional"
"$bsort_bin" -r 4 -k 2 -i "$tmpdir/input-positional" "$tmpdir/output-positional" >/dev/null
cmp "$tmpdir/output-positional" "$tmpdir/expected"

printf 'b2AAa2BBa1CCb1DD' > "$tmpdir/input-output-first"
"$bsort_bin" -r 4 -k 2 -o "$tmpdir/output-first" "$tmpdir/input-output-first" >/dev/null
cmp "$tmpdir/output-first" "$tmpdir/expected"

printf 'b2AAa2BBa1CCb1DD' > "$tmpdir/ascii"
"$bsort_bin" -a -r 4 -k 2 -t 0 "$tmpdir/ascii" >/dev/null
cmp "$tmpdir/ascii" "$tmpdir/expected"

printf 'b2AAa2BBa1CCb1DD' > "$tmpdir/ascii-no-validate"
"$bsort_bin" -a -V -r 4 -k 2 -t 0 "$tmpdir/ascii-no-validate" >/dev/null
cmp "$tmpdir/ascii-no-validate" "$tmpdir/expected"

printf 'AA\177Z' > "$tmpdir/bad-ascii"
if "$bsort_bin" -a -r 4 -k 3 "$tmpdir/bad-ascii" >/dev/null 2>&1; then
  exit 1
fi
