#!/usr/bin/env bash
# DUNE far-detector selection files and oscillation grid (what GUNDAM reads):
#   $data_dir/fakeselect_FHC.root, $data_dir/fakeselect_RHC.root   src/dune/SelectFD.C
#   $data_dir/parameters/oscProb/grid_DUNE.root                    src/MakeOscGrid.C
# The DUNE counterpart of scripts/select.sh + src/FakeSelect.C (HK-specific, not used
# here). Needs root on PATH (source ~/genie_setup.sh) and every <stem>_fakerecon.root
# (scripts/dune/recon.sh). See lbl_config/dune/README.md, "Selection files".
#
#   scripts/dune/select.sh
#
# The input files of each beam mode are the manifest rows (gen/dune/samples.txt) whose
# name ends in _fhc / _rhc, in manifest order. Existing outputs are never overwritten:
# the script exits with an error if any of the three outputs exists.

set -eo pipefail

[ -n "$working_dir" ] || { echo "working_dir is not set"; exit 1; }
cd "$working_dir"
[ -n "$data_dir" ] || { echo "data_dir is not set"; exit 1; }
command -v root >/dev/null || { echo "[ERROR] root not on PATH (source ~/genie_setup.sh)"; exit 1; }

MANIFEST=gen/dune/samples.txt
CHANNELS=lbl_config/dune/tdr2017/channels.tsv
RULES=lbl_config/dune/tdr2017/rules.tsv
MACRO=src/dune/SelectFD.C
GRID_MACRO=src/MakeOscGrid.C
GRID="$data_dir/parameters/oscProb/grid_DUNE.root"
for f in "$MANIFEST" "$CHANNELS" "$RULES" "$MACRO" "$GRID_MACRO"; do
  [ -s "$f" ] || { echo "[ERROR] missing $f"; exit 1; }
done

# root -q exits with the macro's return value; these macros return 1 on success
# (as in scripts/genie/make_covariances.sh).
root_ok() { local rc=0; root -l -b -q "$@" || rc=$?; [ "$rc" -eq 1 ] || { echo "ERROR: root exited with $rc"; return 1; }; }

for f in "$data_dir/fakeselect_FHC.root" "$data_dir/fakeselect_RHC.root" "$GRID"; do
  [ -e "$f" ] && { echo "[ERROR] output exists; move it away first: $f"; exit 1; }
done
[ -d "$(dirname "$GRID")" ] || { echo "[ERROR] missing $(dirname "$GRID") (scripts/init_data_dir.sh)"; exit 1; }

COMMIT=$(git rev-parse HEAD)
DIRTY=$(git status --porcelain --untracked-files=no | wc -l)
UNTRACKED=$(git status --porcelain -- "$MACRO" scripts/dune/select.sh | wc -l)
BASEPROV="git $COMMIT (modified tracked files: $DIRTY; macro/script untracked or modified: $UNTRACKED);"
BASEPROV="$BASEPROV sha256 $MACRO $(sha256sum "$MACRO" | cut -c1-64);"
BASEPROV="$BASEPROV sha256 scripts/dune/select.sh $(sha256sum scripts/dune/select.sh | cut -c1-64);"
BASEPROV="$BASEPROV sha256 $CHANNELS $(sha256sum "$CHANNELS" | cut -c1-64);"
BASEPROV="$BASEPROV sha256 $RULES $(sha256sum "$RULES" | cut -c1-64)"

echo
echo "============================================================"
echo " DUNE FD selection files and oscillation grid"
echo "============================================================"
echo "Host       : $(hostname)"
echo "Start      : $(date)"
echo "ROOT       : $(root-config --version)"
echo "data_dir   : $data_dir"
echo "Provenance : $BASEPROV"
echo "============================================================"

for BEAM in FHC RHC; do
  suffix=$(echo "$BEAM" | tr A-Z a-z)
  INPUTS=""; PROV="$BASEPROV"; i=-1
  while IFS='|' read -r name stem _; do
    i=$((i + 1))
    [[ "$name" == *_"$suffix" ]] || continue
    f="$data_dir/${stem}_fakerecon.root"
    [ -s "$f" ] || { echo "[ERROR] missing $f (scripts/dune/recon.sh)"; exit 1; }
    INPUTS="$INPUTS$i:$f;"
    PROV="$PROV; input row $i $name sha256 $(sha256sum "$f" | cut -c1-64)"
  done < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' "$MANIFEST")
  OUT="$data_dir/fakeselect_$BEAM.root"
  echo
  echo "------------------------------------------------------------"
  echo " $BEAM -> $OUT"
  echo " inputs: $INPUTS"
  echo "------------------------------------------------------------"
  start_seconds=$(date +%s)
  root_ok "$MACRO(\"$BEAM\",\"$OUT\",\"$INPUTS\",\"$CHANNELS\",\"$RULES\",\"$PROV\")" || exit 1
  [ -s "$OUT" ] || { echo "[ERROR] $OUT not written"; exit 1; }
  ls -lh "$OUT"
  echo "Elapsed seconds: $(( $(date +%s) - start_seconds ))"
done

echo
echo "------------------------------------------------------------"
echo " oscillation grid -> $GRID"
echo "------------------------------------------------------------"
# 0.01 GeV bins from 0 to 120 GeV: the flux histograms reach 118 GeV.
root_ok "$GRID_MACRO(\"$GRID\",12000,0.0,120.0)" || exit 1
[ -s "$GRID" ] || { echo "[ERROR] $GRID not written"; exit 1; }
ls -lh "$GRID"

echo
echo "============================================================"
echo " FINISHED $(date)"
echo "============================================================"
