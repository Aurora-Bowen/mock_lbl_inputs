#!/bin/bash
# GENIE cross-section splines for the DUNE far detector: argon-40 (1000180400),
# GENIE 3.06.02, tune AR23_20i_00_000, event-generator list Default, no --xml-path.
# Modelled on scripts/genie/xsec.sh (same gmkspl call and RUN_INFO logging).
# Needs gmkspl and gspladd on PATH (source ~/genie_setup.sh).
#
#   scripts/dune/xsec.sh [-p PDGS] [-e EMAX] [-n KNOTS] [-s SEED] [-o OUT]
#   scripts/dune/xsec.sh merge -o OUT IN1.xml,IN2.xml,...
#
#   -p PDGS   comma-separated neutrino PDG codes        default 12,-12,14,-14,16,-16
#   -e EMAX   maximum neutrino energy [GeV]              default 120 (FD flux histograms reach 118 GeV)
#   -n KNOTS  knots per interaction (gmkspl -n)          default 500 (see below)
#   -s SEED   random seed passed to gmkspl               default 20231109
#   -o OUT    output spline XML                          default $data_dir/dune/xsec/ar23_ar40.xml
#             (data_dir must then be set; otherwise give -o)
#
# A RUN_INFO file is written next to OUT (OUT with .xml replaced by .RUN_INFO.txt).
# One gmkspl run per call; for production run one flavour per job and merge
# (jobs/xsec_ar23.sbatch in the work area does exactly that).
#
# Knot spacing (GENIE 3.06.02 source, XSecSplineList::CreateSpline and
# GEVGDriver::CreateSplines): for each interaction, gmkspl puts 5 linearly
# spaced knots below the interaction threshold Ethr (only if Ethr > Emin), one
# knot on max(Ethr, Emin), and the rest log-spaced up to EMAX; Emin is the
# generator validity minimum, 0.01 GeV (GVLD-Emin in AR23_20i/CommonParam.xml),
# and KNOTS is raised to at least 30. Knots per decade above threshold are
# therefore (KNOTS-6)/log10(EMAX/E0) with E0 = max(Ethr, 0.01 GeV).
# The HK script uses 300 knots up to 30 GeV: 294/log10(30/E0) per decade.
# Requiring the same or higher density for the same E0 with EMAX = 120 GeV:
#   KNOTS - 6 >= 294 * log10(120/E0) / log10(30/E0)
# KNOTS = 500 satisfies this for every E0 <= 3.91 GeV, which includes the
# free-nucleon nu_tau CC quasi-elastic threshold (3.45 GeV). For E0 = 0.01 GeV
# it gives 122 knots/decade (HK: 86); for E0 = 1 GeV, 237 (HK: 199).
# Interactions with thresholds above 3.91 GeV get fewer knots per decade than an
# HK-style run would give them at the same threshold, but never fewer than 122.

usage() {
  sed -n '7,16p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
}

TUNE="AR23_20i_00_000"
TARGET="1000180400"
EVGEN_LIST="Default"
PDGS="12,-12,14,-14,16,-16"
EMAX=120
KNOTS=500
SEED=20231109
OUT=""

merge() {
  OUT=$1; INPUTS=$2
  [ -n "$OUT" ] && [ -n "$INPUTS" ] || usage
  echo "Checking input spline files..."
  IFS=',' read -ra FILES <<< "$INPUTS"
  for p in "${FILES[@]}"; do
    if [ ! -s "$p" ]; then echo "ERROR: missing or empty: $p"; return 1; fi
    ls -lh "$p"
  done
  if [ -e "$OUT" ]; then echo "ERROR: $OUT exists; move it away first"; return 1; fi
  echo "Merging..."
  gspladd -f "$INPUTS" -o "$OUT" || return 1
  [ -s "$OUT" ] || { echo "ERROR: gspladd wrote nothing"; return 1; }
  echo "Done:"
  ls -lh "$OUT"
}

if [ "${1:-}" = "merge" ]; then
  shift
  while getopts "o:" opt; do
    case $opt in o) OUT=$OPTARG ;; *) usage ;; esac
  done
  shift $((OPTIND - 1))
  merge "$OUT" "${1:-}" || exit 1
  exit 0
fi

while getopts "p:e:n:s:o:h" opt; do
  case $opt in
    p) PDGS=$OPTARG ;;
    e) EMAX=$OPTARG ;;
    n) KNOTS=$OPTARG ;;
    s) SEED=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) usage ;;
  esac
done
if [ -z "$OUT" ]; then
  [ -n "$data_dir" ] || { echo "give -o OUT or set data_dir"; exit 1; }
  OUT="$data_dir/dune/xsec/ar23_ar40.xml"
fi
command -v gmkspl >/dev/null || { echo "gmkspl not on PATH (source ~/genie_setup.sh)"; exit 1; }
if [ -e "$OUT" ]; then echo "ERROR: $OUT exists; move it away first"; exit 1; fi
mkdir -p "$(dirname "$OUT")"
INFO="${OUT%.xml}.RUN_INFO.txt"

{
  echo "description   : DUNE FD argon splines, unmodified GENIE tune, no --xml-path override"
  echo "genie         : $(genie-config --version 2>/dev/null)"
  echo "tune          : $TUNE"
  echo "event gen list: $EVGEN_LIST"
  echo "xml-path      : (none)"
  echo "flavours      : $PDGS"
  echo "targets       : $TARGET"
  echo "knots / emax  : $KNOTS / $EMAX GeV"
  echo "seed          : $SEED"
  echo "output        : $OUT"
  echo "host          : $(hostname)"
  echo "slurm         : ${SLURM_JOB_ID:-none}${SLURM_ARRAY_TASK_ID:+ (array task $SLURM_ARRAY_TASK_ID)}"
  echo "started       : $(date -Is)"
} > "$INFO"
cat "$INFO"

start=$(date +%s)

gmkspl \
  -p "$PDGS" \
  -t "$TARGET" \
  -n "$KNOTS" \
  -e "$EMAX" \
  --tune "$TUNE" \
  --event-generator-list "$EVGEN_LIST" \
  --seed "$SEED" \
  -o "$OUT"

status=$?
end=$(date +%s)

{
  echo "finished      : $(date -Is)"
  echo "elapsed       : $(( end - start )) s"
  echo "exit status   : $status"
  [ -f "$OUT" ] && echo "sha256        : $(sha256sum "$OUT" | cut -d' ' -f1)"
} >> "$INFO"
cat "$INFO"

exit $status
