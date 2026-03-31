#!/bin/bash
# bump-version.sh — Update the Lace version number everywhere.
#
# Usage:  ./bump-version.sh 1.6.0
#
# The canonical version lives in CMakeLists.txt.  This script updates it
# there and propagates it to the places that cannot read it automatically:
#   - CMakeLists.txt
#   - README.md

set -euo pipefail

NEW="${1:-}"

if ! [[ "$NEW" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Usage: $0 MAJOR.MINOR.PATCH" >&2
    exit 1
fi

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Portable in-place sed (GNU sed and BSD/macOS sed both accept -i.bak)
sedi() { sed -i.bak "$@" && rm -f "${@: -1}.bak"; }

# 1. CMakeLists.txt
sedi '/^project(/,/)/ s/\(VERSION \)[0-9]*\.[0-9]*\.[0-9]*/\1'"${NEW}"'/' "$REPO_DIR/CMakeLists.txt"

# 2. README.md
sedi "s/find_package(lace [0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*/find_package(lace ${NEW}/g" "$REPO_DIR/README.md"
sedi "s/GIT_TAG *v[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*/GIT_TAG        v${NEW}/g" "$REPO_DIR/README.md"

# 3. Regenerate headers (lace.sh now reads version from CMakeLists.txt)
if [ -x "$REPO_DIR/src/gen.sh" ]; then
    echo "Regenerating headers..."
    (cd "$REPO_DIR/src" && ./gen.sh)
fi

echo "Version updated to ${NEW}"
echo ""
echo "Remaining manual steps:"
echo "  1. Update CHANGELOG.md with the new version and date"
echo "  2. Commit, tag (git tag v${NEW}), and push"
