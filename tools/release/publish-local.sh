#!/bin/bash
# Puts a PsiMail (or PsiWeb) build on the local update server, signed, so
# the Psion can fetch it with Tools > Update PsiMail.
#   core/tools/release/publish-local.sh PsiMail 0.3.1 [dist/PsiMail.sis]
# The server (core/server/psion-update.sh) serves $PSION_UPDATE_DIR
# (default ~/PsionDownloads/update). The .sis defaults to dist/<Product>.sis
# in the program's checkout (PSION_APP, else the folder above this core).
set -e
PRODUCT=${1:?product: PsiMail or PsiWeb}
VERSION=${2:?version, e.g. 0.3.1}
CORE=$(cd "$(dirname "$0")/../.." && pwd)
APP=${PSION_APP:-$(cd "$CORE/.." && pwd)}
SIS=${3:-$APP/dist/$PRODUCT.sis}
DIR="${PSION_UPDATE_DIR:-$HOME/PsionDownloads/update}"
mkdir -p "$DIR"
cp "$SIS" "$DIR/$PRODUCT.sis.new"
python3 "$CORE/tools/release/sign.py" ${PSION_SIGN_KEY:+--key "$PSION_SIGN_KEY"} --product "$PRODUCT" "$DIR/$PRODUCT.sis.new" "$VERSION" >/dev/null
mv "$DIR/$PRODUCT.sis.new" "$DIR/$PRODUCT.sis"
mv "$DIR/$PRODUCT.sis.new.sig" "$DIR/$PRODUCT.sis.sig"
echo "$VERSION" > "$DIR/$PRODUCT-version.txt"
echo "$PRODUCT $VERSION is on the update server ($DIR)"
