#!/bin/bash
# Convert the DUNE TDR far-detector response (GLoBES AEDL text in
# lbl_config/dune/tdr2017/globes_config/) to
#   lbl_config/dune/tdr2017/tdr2017_response.root
#   lbl_config/dune/tdr2017/channels.tsv
#   lbl_config/dune/tdr2017/rules.tsv
# with src/dune/ConvertGlobesResponse.C. No GLoBES software is used.
# Run from the repository root with ROOT set up:
#   scripts/dune/convert_response.sh
# The ACLiC build products go to $BUILD_DIR (default build/aclic, git-ignored).
# Exit status: 0 = all consistency checks passed, 1 = some check failed (see CHECK FAIL lines).

set -eo pipefail
[ -f src/dune/ConvertGlobesResponse.C ] || { echo "run from the repository root"; exit 2; }
command -v root >/dev/null || { echo "root not on PATH"; exit 2; }

D=lbl_config/dune/tdr2017
BUILD_DIR=${BUILD_DIR:-build/aclic}
mkdir -p "$BUILD_DIR"
COMMIT=$(git rev-parse HEAD)
DIRTY=$(git status --porcelain --untracked-files=no | wc -l)
echo "git HEAD: $COMMIT  (modified tracked files: $DIRTY)"

echo "== sha256 of the GLoBES inputs against $D/SHA256SUMS.txt"
(cd $D && grep '  globes_config/' SHA256SUMS.txt | sha256sum -c --quiet) && echo "all globes_config files match SHA256SUMS.txt"

set +e
root -l -b -q \
  -e "gSystem->SetBuildDir(\"$BUILD_DIR\", true);" \
  -e '.L src/dune/ConvertGlobesResponse.C+' \
  -e "int rc = ConvertGlobesResponse(\"$D/globes_config\", \"DUNE_GLoBES.glb\", \"$D/SHA256SUMS.txt\", \"$D/tdr2017_response.root\", \"$D/channels.tsv\", \"$D/rules.tsv\", \"$COMMIT\"); gSystem->Exit(rc);"
rc=$?
echo "converter exit status: $rc"
exit $rc
