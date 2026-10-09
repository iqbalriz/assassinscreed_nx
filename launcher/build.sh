#!/bin/sh
# Build assassinscreed_nx.nro (the launcher) with the runtime's launcher build
# (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../assassinscreed_nx.nsp and ../assassinscreed_nx.build.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=assassinscreed_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
