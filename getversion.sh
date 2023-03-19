#!/bin/bash

TARGET=${1:-cygwin-bridge.exe}
TARGET_BASENAME=$(basename $TARGET .exe)

VER_FILEVERSION="0,0,0,0"
VER_PRODUCTVERSION="0,0,0,0"
VER_FILEVERSION_STR="0.0.0.0"
VER_PRODUCTVERSION_STR="0.0.0.0"

if which git > /dev/null 2>&1; then
  hash=$(git show --format='%h' --no-patch)
  tag=$(git describe --tags)
  if [[ $? -eq 0 ]]; then
    VER_PRODUCTVERSION_STR="$tag ($hash)"
  else
    VER_PRODUCTVERSION_STR="($hash)"
  fi
fi

cat <<EOF
#define VER_INTERNALNAME_STR "$TARGET_BASENAME"
#define VER_ORIGINALFILENAME_STR "$TARGET"
#define VER_FILEVERSION $VER_FILEVERSION
#define VER_PRODUCTVERSION $VER_PRODUCTVERSION
#define VER_FILEVERSION_STR "$VER_FILEVERSION_STR"
#define VER_PRODUCTVERSION_STR "$VER_PRODUCTVERSION_STR"
EOF
