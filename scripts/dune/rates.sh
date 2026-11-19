#!/bin/bash
# Expected DUNE far-detector interactions per sample and events to generate:
#   1. gspl2root for Ar40 and each of the six flavours -> OUTDIR/xsec_<flavour>.root (+ .log);
#      for every probe of a sample with another generator list in gen/dune/samples.txt
#      (the CC list) also gspl2root --event-generator-list <list> -> OUTDIR/xsec_<flavour>_<list>.root
#   2. unit check: numu tot_cc(10 GeV) / 10 / 40 in [0.6, 0.8] (1e-38 cm^2/GeV/nucleon)
#   3. src/dune/ExpectedRates.C -> gen/dune/rates.tsv, its output in OUTDIR/rates.log
# Run from the repository root with GENIE and ROOT set up (source ~/genie_setup.sh):
#
#   scripts/dune/rates.sh -x SPLINE_XML -d OUTDIR [-s all|xsec|rates]
#
#   -x  the official AR23 spline file gxspl-NUsmall.xml (see README, "Splines")
#   -d  directory for the gspl2root files and logs (created if missing)
#   -s  stage: all (default), xsec (steps 1-2), rates (steps 2-3, needs the xsec files)
#
# Every output must be new: the xsec stage refuses to run if any xsec_<flavour>[_<list>].root
# or .log exists (gspl2root opens an existing file in UPDATE mode), the rates stage if
# gen/dune/rates.tsv or OUTDIR/rates.log exists. gspl2root reads the full spline file;
# run the xsec stage on a compute node (inside a Slurm allocation; it refuses otherwise
# unless ALLOW_NO_SLURM=1).
# Environment (both optional; unset = the AR23 production defaults):
#   genie_tune   GENIE tune passed to gspl2root and recorded in the output  default AR23_20i_00_000
#   rates_file   output table, relative to the repository root               default gen/dune/rates.tsv
# Exit status: 0 = done, 1 = a check failed, 2 = setup or I/O error.

set -o pipefail
TUNE=${genie_tune:-AR23_20i_00_000}
TARGET=1000180400
EMIN_EMAX=0.01,120
OUT_TSV=${rates_file:-gen/dune/rates.tsv}
FLAVOURS=(nue:12 nuebar:-12 numu:14 numubar:-14 nutau:16 nutaubar:-16)
MANIFEST=gen/dune/samples.txt

usage() { sed -n '7,11p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
SPL=""; OUTDIR=""; STAGE=all
while getopts "x:d:s:h" opt; do
  case $opt in x) SPL=$OPTARG ;; d) OUTDIR=$OPTARG ;; s) STAGE=$OPTARG ;; *) usage ;; esac
done
[ -n "$SPL" ] && [ -n "$OUTDIR" ] || usage
case "$STAGE" in all|xsec|rates) ;; *) usage ;; esac
[ -f src/dune/ExpectedRates.C ] || { echo "run from the repository root"; exit 2; }
command -v root >/dev/null || { echo "root not on PATH"; exit 2; }
[ -s "$SPL" ] || { echo "ERROR: spline file $SPL missing"; exit 2; }
[ -s "$MANIFEST" ] || { echo "ERROR: $MANIFEST missing"; exit 2; }
fname() { case $1 in 12) echo nue;; -12) echo nuebar;; 14) echo numu;; -14) echo numubar;; 16) echo nutau;; -16) echo nutaubar;; esac; }
# probe:list pairs of the samples whose generator list is not Default (manifest fields 3 and 11)
EXTRA=($(awk -F'|' '!/^[[:space:]]*#/ && NF >= 11 { gsub(/[[:space:]]/, "", $3); gsub(/[[:space:]]/, "", $11); if ($11 != "Default") print $3 ":" $11 }' "$MANIFEST" | sort -u))
for e in "${EXTRA[@]}"; do [ -n "$(fname ${e%%:*})" ] || { echo "ERROR: $MANIFEST: unknown probe ${e%%:*}"; exit 2; }; done
# generator lists of those samples, as assembled by GENIE (tune directory first, then config/)
TUNE_DIR=${TUNE%_*_*}
LISTS_DESC=""
for l in $(printf '%s\n' "${EXTRA[@]}" | cut -d: -f2 | sort -u); do
  ASM=$GENIE/config/$TUNE_DIR/EventGeneratorListAssembler.xml; [ -s "$ASM" ] || ASM=$GENIE/config/EventGeneratorListAssembler.xml
  procs=$(sed -n "/<param_set name=\"$l\">/,/<\/param_set>/p" "$ASM" | sed -n 's/.*genie::EventGenerator\/\([^ <]*\).*/\1/p' | tr '\n' ' ' | sed 's/ $//')
  [ -n "$procs" ] || { echo "ERROR: generator list $l not found in $ASM"; exit 2; }
  LISTS_DESC="$LISTS_DESC${LISTS_DESC:+; }$l = $procs (${ASM#$GENIE/})"
done
mkdir -p "$OUTDIR" || exit 2
OUTDIR=$(cd "$OUTDIR" && pwd)

echo "host       : $(hostname)   slurm job: ${SLURM_JOB_ID:-none}"
echo "git HEAD   : $(git rev-parse HEAD)  (modified tracked files: $(git status --porcelain --untracked-files=no | wc -l))"
echo "splines    : $SPL"
echo "tune       : $TUNE   target: $TARGET   energy: $EMIN_EMAX GeV (log steps)"
echo "outdir     : $OUTDIR"
echo "rates file : $OUT_TSV"
echo "other lists: ${EXTRA[*]:-none} (probe:list from $MANIFEST)"
echo "             ${LISTS_DESC:-}"

unit_check() {
  local v
  v=$(root -l -b -q -e "TFile f(\"$OUTDIR/xsec_numu.root\"); TDirectory *d=0; for (auto k : *f.GetListOfKeys()) if (!strcmp(((TKey*)k)->GetClassName(),\"TDirectoryFile\")) d=(TDirectory*)f.Get(k->GetName()); TGraph *g = d ? (TGraph*)d->Get(\"tot_cc\") : 0; if (g) printf(\"UNIT %s %.6f\\n\", d->GetName(), g->Eval(10.)/10./40.);" 2>&1 | awk '/^UNIT/ {print $2, $3}')
  [ -n "$v" ] || { echo "ERROR: unit check could not read numu tot_cc"; exit 2; }
  echo "unit check : numu ($(cut -d' ' -f1 <<< "$v")) tot_cc(10 GeV)/10/40 = $(cut -d' ' -f2 <<< "$v") [1e-38 cm^2/GeV/nucleon], required 0.6-0.8"
  awk -v x="$(cut -d' ' -f2 <<< "$v")" 'BEGIN { exit !(x >= 0.6 && x <= 0.8) }' || { echo "CHECK FAIL: unit check"; exit 1; }
  echo "unit check : PASS"
}

if [ "$STAGE" = all ] || [ "$STAGE" = xsec ]; then
  command -v gspl2root >/dev/null || { echo "gspl2root not on PATH"; exit 2; }
  [ -n "$SLURM_JOB_ID" ] || [ "${ALLOW_NO_SLURM:-0}" = 1 ] || { echo "ERROR: not inside a Slurm allocation"; exit 2; }
  for fp in "${FLAVOURS[@]}"; do
    f=${fp%%:*}
    for x in "$OUTDIR/xsec_$f.root" "$OUTDIR/xsec_$f.log"; do
      [ -e "$x" ] && { echo "ERROR: $x exists; move it away first"; exit 2; }
    done
  done
  for e in "${EXTRA[@]}"; do
    for x in "$OUTDIR/xsec_$(fname ${e%%:*})_${e##*:}.root" "$OUTDIR/xsec_$(fname ${e%%:*})_${e##*:}.log"; do
      [ -e "$x" ] && { echo "ERROR: $x exists; move it away first"; exit 2; }
    done
  done
  echo "spline sha256: $(sha256sum "$SPL" | cut -d' ' -f1)"
  for fp in "${FLAVOURS[@]}"; do
    f=${fp%%:*}; p=${fp##*:}
    t0=$(date +%s)
    gspl2root -f "$SPL" -p "$p" -t $TARGET --tune $TUNE -e $EMIN_EMAX -l -o "$OUTDIR/xsec_$f.root" > "$OUTDIR/xsec_$f.log" 2>&1
    rc=$?
    nerr=$(grep -c -E ' (FATAL|ERROR) ' "$OUTDIR/xsec_$f.log")
    ncreate=$(grep -c 'Creating cross section spline' "$OUTDIR/xsec_$f.log")
    nnospl=$(grep -c 'No splines loaded for tune' "$OUTDIR/xsec_$f.log")
    echo "gspl2root $f ($p): exit $rc, $(( $(date +%s) - t0 )) s, ERROR/FATAL lines $nerr, 'Creating cross section spline' $ncreate, 'No splines loaded' $nnospl"
    [ $rc -eq 0 ] && [ -s "$OUTDIR/xsec_$f.root" ] || { echo "ERROR: gspl2root failed for $f"; exit 2; }
    [ "$ncreate" -eq 0 ] && [ "$nnospl" -eq 0 ] || { echo "CHECK FAIL: splines missing for $f"; exit 1; }
  done
  for e in "${EXTRA[@]}"; do
    p=${e%%:*}; l=${e##*:}; f=$(fname $p)_$l
    t0=$(date +%s)
    gspl2root -f "$SPL" -p "$p" -t $TARGET --tune $TUNE --event-generator-list $l -e $EMIN_EMAX -l -o "$OUTDIR/xsec_$f.root" > "$OUTDIR/xsec_$f.log" 2>&1
    rc=$?
    nerr=$(grep -c -E ' (FATAL|ERROR) ' "$OUTDIR/xsec_$f.log")
    ncreate=$(grep -c 'Creating cross section spline' "$OUTDIR/xsec_$f.log")
    nnospl=$(grep -c 'No splines loaded for tune' "$OUTDIR/xsec_$f.log")
    echo "gspl2root $f ($p, list $l): exit $rc, $(( $(date +%s) - t0 )) s, ERROR/FATAL lines $nerr, 'Creating cross section spline' $ncreate, 'No splines loaded' $nnospl"
    [ $rc -eq 0 ] && [ -s "$OUTDIR/xsec_$f.root" ] || { echo "ERROR: gspl2root failed for $f"; exit 2; }
    [ "$ncreate" -eq 0 ] && [ "$nnospl" -eq 0 ] || { echo "CHECK FAIL: splines missing for $f"; exit 1; }
  done
  unit_check
fi

if [ "$STAGE" = all ] || [ "$STAGE" = rates ]; then
  [ "$STAGE" = rates ] && unit_check
  [ -e "$OUT_TSV" ] && { echo "ERROR: $OUT_TSV exists; move it away first"; exit 2; }
  [ -e "$OUTDIR/rates.log" ] && { echo "ERROR: $OUTDIR/rates.log exists; move it away first"; exit 2; }
  mkdir -p "$(dirname "$OUT_TSV")"
  SHA=$(sha256sum "$SPL" | cut -d' ' -f1)
  CODE="sha256 rates.sh $(sha256sum scripts/dune/rates.sh | cut -c1-12), ExpectedRates.C $(sha256sum src/dune/ExpectedRates.C | cut -c1-12)"
  BUILD_DIR=${BUILD_DIR:-build/aclic}
  mkdir -p "$BUILD_DIR"
  root -l -b -q \
    -e "gSystem->SetBuildDir(\"$BUILD_DIR\", true);" \
    -e '.L src/dune/ExpectedRates.C+' \
    -e "int rc = ExpectedRates(\"$OUTDIR\", \"lbl_config/dune/tdr2017/flux\", \"lbl_config/dune/tdr2017/tdr2017_response.root\", \"$OUT_TSV\", \"$SPL\", \"$SHA\", \"$TUNE\", \"$(git rev-parse --short HEAD)\", \"$CODE\", \"$LISTS_DESC\"); gSystem->Exit(rc);" \
    2>&1 | tee "$OUTDIR/rates.log"
  rc=$?
  echo "ExpectedRates exit status: $rc"
  exit $rc
fi
exit 0
