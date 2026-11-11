#!/bin/bash
# Uniform-bin (0.125 GeV) copies of the TDR far-detector flux histograms
# numu_flux, numubar_flux, nue_flux, nuebar_flux, for NUISANCE:
#   lbl_config/dune/tdr2017/flux/flux_dune_neutrino_FD.root     -> flux_dune_neutrino_FD_uniform.root
#   lbl_config/dune/tdr2017/flux/flux_dune_antineutrino_FD.root -> flux_dune_antineutrino_FD_uniform.root
# with src/dune/MakeUniformFlux.C. Run from the repository root with ROOT set up:
#   scripts/dune/uniform_flux.sh
# Refuses to run if an output exists. The ACLiC build products go to $BUILD_DIR
# (default build/aclic, git-ignored).
# Exit status: 0 = all checks passed, 1 = some check failed, 2 = setup or I/O error.

set -o pipefail
[ -f src/dune/MakeUniformFlux.C ] || { echo "run from the repository root"; exit 2; }
command -v root >/dev/null || { echo "root not on PATH"; exit 2; }

D=lbl_config/dune/tdr2017/flux
BUILD_DIR=${BUILD_DIR:-build/aclic}
mkdir -p "$BUILD_DIR"
echo "git HEAD: $(git rev-parse HEAD)  (modified tracked files: $(git status --porcelain --untracked-files=no | wc -l))"

echo "== sha256 of the input flux files against lbl_config/dune/tdr2017/SHA256SUMS.txt"
(cd lbl_config/dune/tdr2017 && grep -E '  flux/flux_dune_(neutrino|antineutrino)_FD\.root$' SHA256SUMS.txt | sha256sum -c) || exit 2

for mode in neutrino antineutrino; do
  out="$D/flux_dune_${mode}_FD_uniform.root"
  [ -e "$out" ] && { echo "ERROR: $out exists; move it away first"; exit 2; }
done

rc_all=0
for mode in neutrino antineutrino; do
  in="$D/flux_dune_${mode}_FD.root"
  out="$D/flux_dune_${mode}_FD_uniform.root"
  root -l -b -q \
    -e "gSystem->SetBuildDir(\"$BUILD_DIR\", true);" \
    -e '.L src/dune/MakeUniformFlux.C+' \
    -e "int rc = MakeUniformFlux(\"$in\", \"$out\"); gSystem->Exit(rc);"
  rc=$?
  echo "MakeUniformFlux exit status ($mode): $rc"
  [ $rc -gt $rc_all ] && rc_all=$rc
  [ -e "$out" ] && sha256sum "$out"
done
exit $rc_all
