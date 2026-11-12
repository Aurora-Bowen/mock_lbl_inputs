#!/usr/bin/env bash
# Convert the DUNE FD GENIE GHEP samples to NUISANCE GenericVectors flat trees:
#   GHEP -> PrepareGENIE -> nuisflat -> $data_dir/<stem>_flattree.root
# so that the shared scripts/recon.sh reads them under their NEUT names.
# Modelled on scripts/genie/flat.sh (HK); target Ar40 only. Needs PrepareGENIE and
# nuisflat from a NUISANCE build with GENIE support on PATH, in the same GENIE and ROOT
# environment that generated the events (lbl_config/dune/README.md, "Generation").
#
#   scripts/dune/flat.sh             all samples, in manifest order
#   scripts/dune/flat.sh <sample>    one sample, by name or 0-based row index
#
# Existing outputs are never overwritten: the script exits with an error if the
# prepared GHEP or the flat tree exists.

set -eo pipefail

[ -n "$working_dir" ] || { echo "working_dir is not set"; exit 1; }
cd "$working_dir"
[ -n "$data_dir" ] || { echo "data_dir is not set"; exit 1; }

MANIFEST=gen/dune/samples.txt
TARGET="1000180400[1]"
FLUX_HIST="spectrum"

# Manifest rows without comments and blanks; $1 picks one by name or index.
ROWS=()
while read -r l; do ROWS+=("$l"); done < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' "$MANIFEST")
SEL=()
for i in "${!ROWS[@]}"; do
  IFS='|' read -r name _ <<< "${ROWS[$i]}"
  [ -z "${1:-}" ] || [ "$1" = "$name" ] || [ "$1" = "$i" ] && SEL+=("${ROWS[$i]}")
done
[ ${#SEL[@]} -gt 0 ] || { echo "ERROR: no sample $1 in $MANIFEST"; exit 1; }


# ------------------------------------------------------------
# Environment checks
# ------------------------------------------------------------

command -v PrepareGENIE >/dev/null || { echo "[ERROR] PrepareGENIE not on PATH"; exit 1; }
command -v nuisflat >/dev/null || { echo "[ERROR] nuisflat not on PATH"; exit 1; }
command -v root >/dev/null || { echo "[ERROR] root not on PATH"; exit 1; }

echo
echo "============================================================"
echo " GENIE -> NUISANCE FlatTree Production (DUNE FD)"
echo "============================================================"
echo "Host       : $(hostname)"
echo "ROOT       : $(root-config --version)"
echo "GENIE      : ${GENIE:-not set}"
echo "NUISANCE   : ${NUISANCE:-not set}"
echo "nuisflat   : $(command -v nuisflat)"
echo "PrepareGENIE: $(command -v PrepareGENIE)"
echo "Target     : $TARGET"
echo "============================================================"
echo


# ------------------------------------------------------------
# Conversion loop
# ------------------------------------------------------------

total=${#SEL[@]}
index=0

for row in "${SEL[@]}"; do

    IFS='|' read -r sample stem _ <<< "$row"
    index=$((index + 1))

    sample_dir="$data_dir/genie/$sample"

    ghep="$sample_dir/$sample.ghep.root"
    flux="$sample_dir/input-flux.root"

    prepared="$sample_dir/$sample.prepared.ghep.root"
    flat="$data_dir/${stem}_flattree.root"

    echo
    echo "============================================================"
    echo " [$index/$total] $sample"
    echo "============================================================"
    echo "Start      : $(date)"

    # --------------------------------------------------------
    # Check required input files and that no output exists
    # --------------------------------------------------------

    if [[ ! -f "$ghep" ]]; then
        echo "[ERROR] Missing GHEP file:"
        echo "        $ghep"
        exit 1
    fi

    if [[ ! -f "$flux" ]]; then
        echo "[ERROR] Missing flux file:"
        echo "        $flux"
        exit 1
    fi

    for f in "$prepared" "$flat"; do
        if [[ -e "$f" ]]; then
            echo "[ERROR] Output exists; move it away first:"
            echo "        $f"
            exit 1
        fi
    done

    start_seconds=$(date +%s)

    # --------------------------------------------------------
    # Step 1: PrepareGENIE
    # --------------------------------------------------------

    echo
    echo "[1/2] PrepareGENIE"

    PrepareGENIE \
        -i "$ghep" \
        -f "$flux,$FLUX_HIST" \
        -t "$TARGET" \
        -o "$prepared"

    if [[ ! -s "$prepared" ]]; then
        echo "[ERROR] PrepareGENIE failed to create:"
        echo "        $prepared"
        exit 1
    fi


    # --------------------------------------------------------
    # Step 2: nuisflat
    # --------------------------------------------------------

    echo
    echo "[2/2] nuisflat"

    mkdir -p "$(dirname "$flat")"

    nuisflat \
        -i "GENIE:$prepared" \
        -f GenericVectors \
        -o "$flat"

    if [[ ! -s "$flat" ]]; then
        echo "[ERROR] nuisflat failed to create:"
        echo "        $flat"
        exit 1
    fi

    # --------------------------------------------------------
    # Check: the flat tree has as many entries as the GHEP tree
    # --------------------------------------------------------

    # (root -q exits with the value of the last expression, here printf's character
    # count, so its status is ignored and the printed value decides)
    n_ghep=$(root -l -b -q -e "TFile f(\"$ghep\"); TTree *t=(TTree*)f.Get(\"gtree\"); printf(\"N %lld\n\", t?t->GetEntries():-1LL);" 2>&1 | awk '/^N / {print $2}' || true)
    n_flat=$(root -l -b -q -e "TFile f(\"$flat\"); TTree *t=(TTree*)f.Get(\"FlatTree_VARS\"); printf(\"N %lld\n\", t?t->GetEntries():-1LL);" 2>&1 | awk '/^N / {print $2}' || true)
    echo "entries: gtree ${n_ghep:-unreadable}, FlatTree_VARS ${n_flat:-unreadable}"
    if [[ -z "$n_ghep" || "$n_ghep" != "$n_flat" ]]; then
        echo "[ERROR] flat tree entries differ from the GHEP entries"
        exit 1
    fi

    echo
    echo "[OK] $sample"
    echo "     FlatTree: $flat"
    ls -lh "$prepared" "$flat"
    echo "     Elapsed seconds: $(( $(date +%s) - start_seconds ))"
    echo "End        : $(date)"

done


echo
echo "============================================================"
echo " $total SAMPLE(S) FINISHED"
echo "============================================================"
echo
