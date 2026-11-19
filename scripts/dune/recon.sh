#!/usr/bin/env bash
# DUNE far-detector fake reconstruction: fold each flat tree with the TDR GLoBES
# response (src/dune/FoldResponse.C):
#   $data_dir/<stem>_flattree.root -> $data_dir/<stem>_fakerecon.root
# The DUNE counterpart of scripts/recon.sh + src/FakeRecon.C (HK-specific, not used
# here). Needs root on PATH (source ~/genie_setup.sh). See lbl_config/dune/README.md,
# "Fake reconstruction".
#
#   scripts/dune/recon.sh             all samples, in manifest order
#   scripts/dune/recon.sh <sample>    one sample, by name or 0-based row index
#
# Per sample it reads the manifest row (gen/dune/samples.txt: stem, flux_pdg, seed,
# nevents) and the rates.tsv row (beam, N, nevents), and passes them to the macro.
# The random seed is the manifest seed, so the same input gives the same output.
# Environment (optional): rates_file = the table N is read from (default gen/dune/rates.tsv).
# With the default table the manifest and table nevents must agree, as before; with another
# table (e.g. gen/dune/rates_g00b.tsv for tune G00_00b_00_000) only probe pdg and generator
# list must agree, and nevents is always the manifest count (the events actually generated).
# The table, its sha256 and the tune named in its header are written into the provenance.
# Existing outputs are never overwritten: the script exits with an error if the
# fakerecon file exists.

set -eo pipefail

[ -n "$working_dir" ] || { echo "working_dir is not set"; exit 1; }
cd "$working_dir"
[ -n "$data_dir" ] || { echo "data_dir is not set"; exit 1; }
command -v root >/dev/null || { echo "[ERROR] root not on PATH (source ~/genie_setup.sh)"; exit 1; }

MANIFEST=gen/dune/samples.txt
RATES=${rates_file:-gen/dune/rates.tsv}
RESPONSE=lbl_config/dune/tdr2017/tdr2017_response.root
CHANNELS=lbl_config/dune/tdr2017/channels.tsv
RULES=lbl_config/dune/tdr2017/rules.tsv
MACRO=src/dune/FoldResponse.C
for f in "$MANIFEST" "$RATES" "$RESPONSE" "$CHANNELS" "$RULES" "$MACRO"; do
  [ -s "$f" ] || { echo "[ERROR] missing $f"; exit 1; }
done

# Manifest rows without comments and blanks; $1 picks one by name or index.
ROWS=()
while read -r l; do ROWS+=("$l"); done < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' "$MANIFEST")
SEL=()
for i in "${!ROWS[@]}"; do
  IFS='|' read -r name _ <<< "${ROWS[$i]}"
  [ -z "${1:-}" ] || [ "$1" = "$name" ] || [ "$1" = "$i" ] && SEL+=("${ROWS[$i]}")
done
[ ${#SEL[@]} -gt 0 ] || { echo "ERROR: no sample $1 in $MANIFEST"; exit 1; }

# Provenance recorded in every output file.
COMMIT=$(git rev-parse HEAD)
DIRTY=$(git status --porcelain --untracked-files=no | wc -l)
UNTRACKED=$(git status --porcelain -- "$MACRO" "$0" | wc -l)
PROV="git $COMMIT (modified tracked files: $DIRTY; macro/script untracked or modified: $UNTRACKED);"
PROV="$PROV sha256 $MACRO $(sha256sum "$MACRO" | cut -c1-64);"
PROV="$PROV sha256 scripts/dune/recon.sh $(sha256sum scripts/dune/recon.sh | cut -c1-64);"
PROV="$PROV sha256 $RESPONSE $(sha256sum "$RESPONSE" | cut -c1-64);"
PROV="$PROV sha256 $CHANNELS $(sha256sum "$CHANNELS" | cut -c1-64);"
PROV="$PROV sha256 $RULES $(sha256sum "$RULES" | cut -c1-64);"
RATES_TUNE=$(sed -n 's/^# splines: .*; tune \([^;]*\);.*/\1/p' "$RATES" | head -1)
PROV="$PROV rates file $RATES sha256 $(sha256sum "$RATES" | cut -c1-64) (tune ${RATES_TUNE:-not found in header})"

echo
echo "============================================================"
echo " DUNE FD fake reconstruction (response folding)"
echo "============================================================"
echo "Host       : $(hostname)"
echo "Start      : $(date)"
echo "ROOT       : $(root-config --version)"
echo "data_dir   : $data_dir"
echo "Provenance : $PROV"
echo "============================================================"

total=${#SEL[@]}
index=0
for row in "${SEL[@]}"; do
  IFS='|' read -r NAME STEM PDG _ _ NEVENTS _ SEED _ FLUX_PDG LIST <<< "$row"
  index=$((index + 1))

  # rates.tsv row: sample beam flux_file flux_hist probe_pdg list N_cc N_nc N nevents
  RROW=$(awk -F'\t' -v s="$NAME" '!/^#/ && $1 == s' "$RATES")
  [ -n "$RROW" ] || { echo "[ERROR] $NAME not in $RATES"; exit 1; }
  IFS=$'\t' read -r _ R_BEAM _ _ R_PDG R_LIST _ _ R_N R_NEV <<< "$RROW"
  if [ -z "${rates_file:-}" ]; then
    [ "$R_NEV" = "$NEVENTS" ] && [ "$R_PDG" = "$PDG" ] && [ "$R_LIST" = "$LIST" ] || {
      echo "[ERROR] $NAME: manifest (nevents $NEVENTS, pdg $PDG, list $LIST) and $RATES ($R_NEV, $R_PDG, $R_LIST) differ"; exit 1; }
  else
    [ "$R_PDG" = "$PDG" ] && [ "$R_LIST" = "$LIST" ] || {
      echo "[ERROR] $NAME: manifest (pdg $PDG, list $LIST) and $RATES ($R_PDG, $R_LIST) differ"; exit 1; }
  fi
  BEAM=$(echo "$R_BEAM" | tr a-z A-Z)

  IN="$data_dir/${STEM}_flattree.root"
  OUT="$data_dir/${STEM}_fakerecon.root"

  echo
  echo "============================================================"
  echo " [$index/$total] $NAME  ($BEAM, flux pdg $FLUX_PDG, probe $PDG, list $LIST)"
  echo "============================================================"
  echo "Start      : $(date)"
  echo "Input      : $IN"
  echo "Output     : $OUT"
  echo "N, nevents : $R_N, $NEVENTS ; seed $SEED"
  [ -f "$IN" ] || { echo "[ERROR] missing flat tree $IN"; exit 1; }
  [ -e "$OUT" ] && { echo "[ERROR] output exists; move it away first: $OUT"; exit 1; }

  start_seconds=$(date +%s)
  # root -q exits with the macro's return value, so the status is ignored and the
  # FOLD_OK line decides.
  LOGTXT=$(root -l -b -q "$MACRO(\"$IN\",\"$OUT\",\"$NAME\",\"$BEAM\",$FLUX_PDG,$R_N,$NEVENTS,$SEED,\"$RESPONSE\",\"$CHANNELS\",\"$RULES\",\"$PROV\")" 2>&1 || true)
  echo "$LOGTXT" | grep -v '^$' | grep -v '^Processing ' || true
  if ! grep -q '^FOLD_OK ' <<< "$LOGTXT"; then
    echo "[ERROR] FoldResponse failed for $NAME"
    exit 1
  fi
  ls -lh "$OUT"
  echo "Elapsed seconds: $(( $(date +%s) - start_seconds ))"
  echo "End        : $(date)"
done

echo
echo "============================================================"
echo " $total SAMPLE(S) FINISHED"
echo "============================================================"
