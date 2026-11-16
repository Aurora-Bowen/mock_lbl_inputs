#!/usr/bin/env bash
# DUNE far-detector parameter files for GUNDAM. Writes into $data_dir/parameters/:
#   oscProb/oscCovDUNE.root   src/dune/MakeOscCov.C (arXiv:2006.16043 Table 8, NO)
# The oscillation grid (oscProb/grid_DUNE.root) is written by scripts/dune/select.sh.
# Needs root on PATH. Refuses to overwrite an existing output.
#
#   scripts/dune/make_covariances.sh
set -eo pipefail
[ -n "$working_dir" ] || { echo "working_dir is not set"; exit 1; }
cd "$working_dir"
[ -n "$data_dir" ] || { echo "data_dir is not set"; exit 1; }
command -v root >/dev/null || { echo "[ERROR] root not on PATH (source ~/genie_setup.sh)"; exit 1; }

# root -q exits with the macro's return value; the macro returns 1 on success
# (as in scripts/genie/make_covariances.sh).
root_ok() { local rc=0; root -l -b -q "$@" || rc=$?; [ "$rc" -eq 1 ] || { echo "ERROR: root exited with $rc"; return 1; }; }

P="$data_dir/parameters/oscProb"
OUT="$P/oscCovDUNE.root"
[ -d "$P" ] || { echo "[ERROR] missing $P (scripts/init_data_dir.sh)"; exit 1; }
[ -e "$OUT" ] && { echo "[ERROR] output exists; move it away first: $OUT"; exit 1; }
echo "git $(git rev-parse HEAD); sha256 src/dune/MakeOscCov.C $(sha256sum src/dune/MakeOscCov.C | cut -c1-64)"
root_ok "src/dune/MakeOscCov.C(\"$OUT\")" || exit 1
[ -s "$OUT" ] || { echo "[ERROR] $OUT not written"; exit 1; }
ls -l "$OUT"
