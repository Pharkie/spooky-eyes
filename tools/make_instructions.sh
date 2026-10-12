#!/usr/bin/env bash
# Renders the one-page WiFi upgrade instructions to PDF with headless Chrome.
#
#   tools/make_instructions.sh                      -> docs/instructions/2026-wifi-upgrade.pdf (generic)
#   tools/make_instructions.sh "Jack's Pumpkin"    -> docs/instructions/2026-wifi-upgrade-jacks-pumpkin.pdf
#   tools/make_instructions.sh "Jack's Pumpkin" "Ruby's Pumpkin"   (one PDF each)
#
# A name personalises the WiFi name and web address in the text. The web address follows the firmware's rule
# (settings::hostnameFor): lowercase letters and digits kept, apostrophes dropped, anything else becomes one hyphen.
# Fonts come from Google Fonts, so render while online; offline, Mac fallbacks are used (and a warning is printed).
# Needs Google Chrome, python3 and pdfinfo/pdffonts (poppler).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
html="$root/docs/instructions/2026-wifi-upgrade.html"
outdir="$root/docs/instructions"
chrome="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

[[ -x "$chrome" ]] || { echo "Can't find Google Chrome at: $chrome (set CHROME=...)" >&2; exit 1; }
command -v pdfinfo >/dev/null || { echo "Needs pdfinfo (brew install poppler)" >&2; exit 1; }

hostname_for() { # same rule as the firmware, max 32 characters
  local host
  host="$(printf '%s' "$1" | sed -e "s/[']//g" -e "s/‘//g" -e "s/’//g" | LC_ALL=C tr '[:upper:]' '[:lower:]' \
          | LC_ALL=C sed -e 's/[^a-z0-9][^a-z0-9]*/-/g' -e 's/^-//' -e 's/-$//' | cut -c1-32 | sed -e 's/-$//')"
  printf '%s' "${host:-pumpkin}"
}

render() { # $1 = pumpkin name ("" for the generic sheet)
  local name="$1" out url host
  url="$(python3 -c 'import pathlib, sys; print(pathlib.Path(sys.argv[1]).as_uri())' "$html")"
  if [[ -n "$name" ]]; then
    host="$(hostname_for "$name")"
    out="$outdir/2026-wifi-upgrade-$host.pdf"
    url="$url?$(python3 -c 'import sys, urllib.parse; print(urllib.parse.urlencode({"name": sys.argv[1], "host": sys.argv[2]}))' "$name" "$host")"
  else
    out="$outdir/2026-wifi-upgrade.pdf"
  fi

  # Headless Chrome uses a throwaway profile of its own. (Passing --user-data-dir made it hang after printing.)
  # The virtual time budget gives the web fonts time to arrive; the watchdog stops a stuck Chrome after 90 s.
  rm -f "$out"
  "$chrome" --headless --disable-gpu --no-first-run --no-default-browser-check \
            --no-pdf-header-footer --virtual-time-budget=20000 --print-to-pdf="$out" "$url" >/dev/null 2>&1 &
  local pid=$! waited=0
  while kill -0 "$pid" 2>/dev/null && (( waited < 90 )); do sleep 1; waited=$((waited + 1)); done
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    echo "Chrome took over 90 s, so it was stopped" >&2
  fi
  wait "$pid" 2>/dev/null || true

  [[ -s "$out" ]] || { echo "Chrome didn't write $out" >&2; return 1; }
  local pages size
  pages="$(pdfinfo "$out" | awk '/^Pages:/ { print $2 }')"
  size="$(pdfinfo "$out" | awk -F': *' '/^Page size:/ { print $2 }')"
  if [[ "$pages" != "1" ]]; then
    echo "ERROR: $out has $pages pages, not 1. Something overflowed: check the HTML." >&2
    return 1
  fi
  if command -v pdftotext >/dev/null && pdftotext "$out" - | grep -q "LAYOUT PROBLEM"; then
    echo "ERROR: $out: $(pdftotext "$out" - | grep -o "LAYOUT PROBLEM.*")" >&2
    return 1
  fi
  if command -v pdffonts >/dev/null && ! pdffonts "$out" | grep -q "EBGaramond"; then
    echo "WARNING: Google Fonts didn't load (offline?), so $out uses fallback fonts." >&2
  fi
  echo "Wrote ${out#"$root"/} (1 page, $size)${name:+ for \"$name\" -> $host.local}"
}

if [[ $# -eq 0 ]]; then
  render ""
else
  for name in "$@"; do render "$name"; done
fi
