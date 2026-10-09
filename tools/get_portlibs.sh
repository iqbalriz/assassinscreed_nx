#!/bin/sh
# Put the AArch32 Mesa build (mesa32's lib/ and include/) into ./portlibs32,
# where the Makefile looks for the renderer. Docker cannot follow a link out of
# the mounted project, so it is a copy.
#
# By default it downloads mesa32's release (github.com/aks796/mesa32, about
# 5 MB). If you built mesa32 yourself, set MESA32 to its prefix folder and
# nothing is downloaded.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
if [ -n "$MESA32" ]; then
  SRC="$MESA32"
else
  TMP="$(mktemp -d)"
  URL="https://github.com/aks796/mesa32/releases/download/release/mesa32.zip"
  echo "downloading $URL"
  curl -fL "$URL" -o "$TMP/mesa32.zip"
  unzip -q "$TMP/mesa32.zip" -d "$TMP"
  SRC="$TMP/mesa32"
fi
if [ ! -f "$SRC/lib/libEGL.a" ]; then
  echo "get_portlibs.sh: no Mesa build at $SRC (lib/libEGL.a is missing)" >&2
  exit 1
fi
mkdir -p "$HERE/portlibs32"
cp -R "$SRC/lib" "$SRC/include" "$HERE/portlibs32/"
echo "portlibs32/ <- $SRC"
