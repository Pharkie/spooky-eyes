#!/usr/bin/env bash
# Rebuilds the artwork in this folder from the 2024 instructions sheet, so the 2026 sheet matches it.
# Needs pdfimages (poppler) and ImageMagick 7 (magick). Takes a minute or two.
# The 2024 sheet isn't in the repo (it has a personal sign-off): pass its path, or keep it in local/ at the
# top of the main checkout (gitignored).
#
#   docs/instructions/assets/extract-2024-artwork.sh [path/to/2024-spooky-pumpkin-eyes-instructions.pdf]
#
#   2024-frame.jpg      the whole 2024 page with its text, QR code and tip boxes painted out (frame + raven kept)
#   2024-header.png     "Knowles & Skrodermoose / The All Hallows / Spooky Pumpkin Eyes(TM)" as ink on transparent
#   2024-bulb.png       the light bulb from the tip boxes
#
# Coordinates are pixels in the 2024 page image (2481 x 3509, 300 dpi).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
pdf="${1:-$here/../../../local/2024-spooky-pumpkin-eyes-instructions.pdf}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

pdfimages -png "$pdf" "$tmp/page"
src="$tmp/page-000.png"

# Areas that held 2024 text (kept clear of the frame, its decorations and the raven)
cat > "$tmp/areas.mvg" <<'EOF'
fill white
rectangle 360,515 2130,1190
rectangle 500,1190 2110,2165
rectangle 500,2165 1660,2625
rectangle 280,2615 1645,3150
EOF
magick -size 2481x3509 xc:black -draw @"$tmp/areas.mvg" "$tmp/areas.png"

# Ink = anything darker than the paper, grown a little to catch anti-aliased edges
magick "$src" -colorspace Gray -threshold 80% -negate -morphology Dilate Disk:5 "$tmp/ink.png"
magick "$tmp/ink.png" "$tmp/areas.png" -compose Multiply -composite -blur 0x1.5 "$tmp/erase.png"

# Paint the paper back in: average the nearby paper (normalised blur), at three sizes, finest where possible
magick "$tmp/ink.png" -negate "$tmp/paper.png"
magick "$src" "$tmp/paper.png" -compose Multiply -composite "$tmp/inked-out.png"
estimate() { # $1 = scale %, $2 = blur sigma at that scale, $3 = output name
  magick "$tmp/inked-out.png" -resize "$1%" -blur "0x$2" -resize 2481x3509! "$tmp/num.png"
  magick "$tmp/paper.png" -resize "$1%" -blur "0x$2" -resize 2481x3509! "$tmp/den-$3.png"
  magick "$tmp/num.png" "$tmp/den-$3.png" -compose DivideDst -composite "$tmp/est-$3.png"
}
estimate 50 6 fine
estimate 25 7.5 mid
estimate 25 20 coarse
magick "$tmp/den-fine.png" -level 25%,45% "$tmp/use-fine.png"
magick "$tmp/den-mid.png" -level 15%,35% "$tmp/use-mid.png"
magick "$tmp/est-coarse.png" "$tmp/est-mid.png" "$tmp/use-mid.png" -composite \
       "$tmp/est-fine.png" "$tmp/use-fine.png" -composite \
       -attenuate 0.35 +noise Gaussian "$tmp/fill.png"
magick "$src" "$tmp/fill.png" "$tmp/erase.png" -composite -quality 84 -sampling-factor 4:2:0 "$here/2024-frame.jpg"

# Ink-on-transparent cut-outs
ink() { # $1 = crop geometry, $2 = output
  magick "$src" -crop "$1" +repage -colorspace Gray -level 22%,80% -negate "$tmp/alpha.png"
  local size
  size="$(magick identify -format "%wx%h" "$tmp/alpha.png")"
  magick -size "$size" xc:"#16110e" "$tmp/alpha.png" -alpha off -compose CopyOpacity -composite \
         -define png:color-type=6 "$2"
}
ink 1770x603+360+515 "$tmp/header.png"
# clear two specks of frame that sneak into the top corners of the header crop
magick "$tmp/header.png" -alpha on \
       -region 300x45+0+0 -channel A -evaluate set 0 +channel +region \
       -region 200x45+1570+0 -channel A -evaluate set 0 +channel +region "$here/2024-header.png"
ink 100x120+364+2695 "$here/2024-bulb.png"

echo "Wrote 2024-frame.jpg, 2024-header.png, 2024-bulb.png to $here"
