#!/bin/bash
# Download the official GENIE AR23 spline package from the SciSoft index, record and
# check its sha256, and extract it. Refuses to overwrite anything.
#
#   scripts/dune/get_xsec.sh -d DIR [-x EXTRACT_DIR]
#
#   -d DIR          where the tarball and SHA256SUMS.txt are written (created if missing)
#   -x EXTRACT_DIR  where the tarball is unpacked                      default DIR
#
# The download link is taken from the index page (not constructed): the href whose text is
# exactly $FILE on $INDEX_URL. SciSoft offers no checksum file for this tarball, so the
# expected sha256 below is the one recorded at the first download (2026-11-10); a mismatch
# stops the script before extraction. Needs curl, sha256sum, tar with bzip2.
# If DIR already holds the tarball (e.g. copied in by hand), it is not downloaded again:
# its sha256 is checked against the recorded value and it is extracted.

INDEX_URL="https://scisoft.fnal.gov/scisoft/packages/genie_xsec/v3_06_00/"
FILE="genie_xsec-3.06.00-noarch-AR2320i00000-k250-e1000.tar.bz2"
EXPECTED_SHA256="f435a98f5d491a181333f27a9a0b675cb94c326770447b0e98d5190983962dd0"

usage() { sed -n '5,8p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

DIR=""; XDIR=""
while getopts "d:x:h" opt; do
  case $opt in d) DIR=$OPTARG ;; x) XDIR=$OPTARG ;; *) usage ;; esac
done
[ -n "$DIR" ] || usage
XDIR=${XDIR:-$DIR}
TAR="$DIR/$FILE"
SUMS="$DIR/SHA256SUMS.txt"
mkdir -p "$DIR" "$XDIR" || exit 1

if [ -e "$TAR" ]; then
  echo "tarball    : $TAR already present, not downloading"
else
  [ -e "$SUMS" ] && { echo "ERROR: $SUMS exists without the tarball; move it away first"; exit 1; }

  echo "index      : $INDEX_URL"
  INDEX=$(curl -sSfL -m 120 "$INDEX_URL") || { echo "ERROR: cannot fetch the index page"; exit 1; }
  HREF=$(grep -o "href=\"[^\"]*\"" <<< "$INDEX" | sed 's/^href="//; s/"$//' | grep -Fx -- "$FILE" | head -1)
  [ -z "$HREF" ] && HREF=$(grep -o "href=\"[^\"]*\"" <<< "$INDEX" | sed 's/^href="//; s/"$//' | grep -F -- "/$FILE" | head -1)
  [ -n "$HREF" ] || { echo "ERROR: no link to $FILE on the index page"; exit 1; }
  case "$HREF" in
    http://*|https://*) URL=$HREF ;;
    /*) URL="$(sed -E 's#^(https?://[^/]+).*#\1#' <<< "$INDEX_URL")$HREF" ;;
    *) URL="${INDEX_URL%/}/$HREF" ;;
  esac
  echo "link       : $HREF"
  echo "url        : $URL"

  start=$(date +%s)
  curl -sSfL -m 7200 -o "$TAR.part" -w "http %{http_code}, %{size_download} bytes, %{time_total} s\n" "$URL" \
    || { echo "ERROR: download failed"; exit 1; }
  end=$(date +%s)
  mv "$TAR.part" "$TAR"
  echo "downloaded : $(date -Is), $((end - start)) s, $(stat -c %s "$TAR") bytes"
fi

SHA=$(sha256sum "$TAR" | cut -d' ' -f1)
if [ -e "$SUMS" ]; then
  grep -q "^$SHA  $FILE\$" "$SUMS" || { echo "ERROR: sha256 $SHA does not match $SUMS"; exit 1; }
  echo "sha256     : $SHA (matches $SUMS)"
else
  echo "$SHA  $FILE" > "$SUMS"
  echo "sha256     : $SHA (written to $SUMS)"
fi
if [ "$SHA" != "$EXPECTED_SHA256" ]; then
  echo "ERROR: sha256 differs from the recorded $EXPECTED_SHA256; not extracting"; exit 1
else
  echo "sha256     : matches the recorded value"
fi

# refuse to overwrite: every path in the archive must be new in XDIR
clash=0
while read -r p; do
  [ -e "$XDIR/$p" ] && [ ! -d "$XDIR/$p" ] && { echo "ERROR: would overwrite $XDIR/$p"; clash=1; }
done < <(tar -tjf "$TAR")
[ $clash = 0 ] || exit 1
tar -xjf "$TAR" -C "$XDIR" --keep-old-files || { echo "ERROR: extraction failed"; exit 1; }
echo "extracted to $XDIR:"
tar -tjf "$TAR" | while read -r p; do [ -f "$XDIR/$p" ] && printf '  %12d  %s\n' "$(stat -c %s "$XDIR/$p")" "$p"; done
