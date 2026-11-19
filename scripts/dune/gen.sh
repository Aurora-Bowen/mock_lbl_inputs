#!/bin/bash
# DUNE far-detector GENIE event generation with gevgen, one run per row of
# gen/dune/samples.txt. Modelled on scripts/genie/gen.sh (HK). Needs gevgen on PATH
# (source ~/genie_setup.sh) and the official AR23 spline file (lbl_config/dune/README.md,
# "Splines") in $xsec_file.
#
#   scripts/dune/gen.sh                      all samples, in manifest order
#   scripts/dune/gen.sh <sample> [nevents]   one sample, by name or 0-based row index;
#                                            nevents overrides the manifest count
#
# Each sample runs in $data_dir/genie/<sample>/ and writes <sample>.ghep.root there, the
# gevgen log <sample>.gevgen.log, and the input-flux.root that gevgen leaves in its working
# directory and scripts/dune/flat.sh reads. Existing outputs are never overwritten: the
# script exits with an error if any of them exists.
# After gevgen it checks that the GHEP tree gtree has exactly the requested number of
# entries and that no cross-section spline was computed on the fly.
# Environment (optional): genie_tune = GENIE tune passed to gevgen (default AR23_20i_00_000;
# the spline file in $xsec_file must hold that tune); dry_run=1 prints, for each selected
# sample, the gevgen command one argument per line (lines "GEVGEN_ARG <i> <arg>") and exits
# without creating or checking any output.
set -o pipefail

[ -n "$working_dir" ] || { echo "working_dir is not set"; exit 1; }
cd "$working_dir" || exit 1
[ -n "$data_dir" ] || { echo "data_dir is not set"; exit 1; }
[ -n "$xsec_file" ] || { echo "xsec_file is not set (official AR23 spline file gxspl-NUsmall.xml)"; exit 1; }
[ -s "$xsec_file" ] || { echo "ERROR: xsec file missing: $xsec_file"; exit 1; }
command -v gevgen >/dev/null || { echo "gevgen not on PATH (source ~/genie_setup.sh)"; exit 1; }
command -v root >/dev/null || { echo "root not on PATH"; exit 1; }

MANIFEST=gen/dune/samples.txt
MSG="$working_dir/gen/genie/Messenger_production.xml"   # the HK Messenger file, reused
TARGET="1000180400"
TUNE="${genie_tune:-AR23_20i_00_000}"
EMIN_EMAX="0,120"

# Manifest rows without comments and blanks; $1 picks one by name or index.
ROWS=()
while read -r l; do ROWS+=("$l"); done < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' "$MANIFEST")
SEL=()
for i in "${!ROWS[@]}"; do
  IFS='|' read -r name _ <<< "${ROWS[$i]}"
  [ -z "$1" ] || [ "$1" = "$name" ] || [ "$1" = "$i" ] && SEL+=("${ROWS[$i]}")
done
[ ${#SEL[@]} -gt 0 ] || { echo "ERROR: no sample $1 in $MANIFEST"; exit 1; }
NEV_OVERRIDE="${2:-}"
if [ -n "$NEV_OVERRIDE" ]; then
  [[ "$NEV_OVERRIDE" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: nevents must be a positive integer: $NEV_OVERRIDE"; exit 1; }
  [ ${#SEL[@]} -eq 1 ] || { echo "ERROR: nevents override needs a single sample"; exit 1; }
fi

XSEC_SHA=$(sha256sum "$xsec_file" | cut -d' ' -f1)

for row in "${SEL[@]}"; do
  IFS='|' read -r NAME STEM PDG FLUX HIST NEVENTS RUN SEED _ _ LIST <<< "$row"
  [ -n "$LIST" ] || { echo "ERROR: manifest row $NAME has no list column"; exit 1; }
  [ -n "$NEV_OVERRIDE" ] && NEVENTS=$NEV_OVERRIDE
  FLUX="$working_dir/$FLUX"

  RUNDIR="$data_dir/genie/$NAME"
  OUT="$RUNDIR/${NAME}.ghep.root"
  LOG="$RUNDIR/${NAME}.gevgen.log"
  INFLUX="$RUNDIR/input-flux.root"

  GEVGEN_CMD=(gevgen
    -r "$RUN"
    -n "$NEVENTS"
    -e "$EMIN_EMAX"
    -p "$PDG"
    -t "$TARGET"
    -f "$FLUX,$HIST,NOWIDTH"
    -F 0,0,1,0,0,0,0
    -o "$OUT"
    --seed "$SEED"
    --cross-sections "$xsec_file"
    --tune "$TUNE"
    --event-generator-list "$LIST"
    --message-thresholds "$MSG")
  if [ "${dry_run:-0}" = 1 ]; then
    echo "GEVGEN_SAMPLE $NAME"
    for i in "${!GEVGEN_CMD[@]}"; do echo "GEVGEN_ARG $i ${GEVGEN_CMD[$i]}"; done
    continue
  fi

  mkdir -p "$RUNDIR" || exit 1
  for f in "$OUT" "$LOG" "$INFLUX"; do
    [ -e "$f" ] && { echo "ERROR: $f exists; move it away first"; exit 1; }
  done
  cd "$RUNDIR" || exit 1

  echo "=================================================="
  echo "GENIE DUNE FD production"
  echo "Sample       : $NAME"
  echo "Host         : $(hostname)"
  echo "Start        : $(date)"
  echo "Probe PDG    : $PDG"
  echo "Events       : $NEVENTS${NEV_OVERRIDE:+ (override; manifest has $(IFS='|' read -r _ _ _ _ _ n _ <<< "$row"; echo "$n"))}"
  echo "Energy range : $EMIN_EMAX GeV"
  echo "Flux file    : $FLUX"
  echo "Flux hist    : $HIST"
  echo "Target       : $TARGET"
  echo "Run number   : $RUN"
  echo "Seed         : $SEED"
  echo "XSec         : $xsec_file"
  echo "XSec sha256  : $XSEC_SHA"
  echo "Tune         : $TUNE"
  echo "Generators   : $LIST"
  echo "Messenger    : $MSG"
  echo "Output       : $OUT"
  echo "Log          : $LOG"
  echo "=================================================="

  [ -s "$FLUX" ] || { echo "ERROR: flux file missing"; exit 1; }
  [ -s "$MSG" ] || { echo "ERROR: messenger config missing"; exit 1; }

  start_seconds=$(date +%s)

  "${GEVGEN_CMD[@]}" 2>&1 | tee "$LOG"

  status=${PIPESTATUS[0]}

  end_seconds=$(date +%s)
  elapsed=$((end_seconds-start_seconds))

  echo "=================================================="
  echo "gevgen exit code: $status"
  echo "Elapsed seconds: $elapsed"
  echo "Elapsed minutes: $((elapsed/60))"
  echo "End: $(date)"
  [ -f "$OUT" ] && ls -lh "$OUT"
  [ "$status" -eq 0 ] || { echo "ERROR: gevgen failed"; exit "$status"; }

  # checks: no spline computed on the fly; gtree has exactly NEVENTS entries
  ncreate=$(grep -c 'Creating cross section spline' "$LOG")
  echo "'Creating cross section spline' lines: $ncreate"
  [ "$ncreate" -eq 0 ] || { echo "ERROR: gevgen computed splines on the fly (xsec file incomplete?)"; exit 1; }
  entries=$(root -l -b -q -e "TFile f(\"$OUT\"); TTree *t=(TTree*)f.Get(\"gtree\"); printf(\"GTREE_ENTRIES %lld\n\", t?t->GetEntries():-1LL);" 2>&1 | awk '/^GTREE_ENTRIES/ {print $2}')
  echo "gtree entries: ${entries:-unreadable} (requested $NEVENTS)"
  [ "${entries:-x}" = "$NEVENTS" ] || { echo "ERROR: gtree does not have exactly $NEVENTS entries"; exit 1; }
  echo "=================================================="
done
