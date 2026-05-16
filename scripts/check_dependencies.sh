#!/bin/bash
set -e

echo "Verifying dependencies..."

# Verify CLI11.hpp exists
if [ ! -f "include/CLI11.hpp" ]; then
    echo "Error: include/CLI11.hpp not found!"
    exit 1
fi

# Verify CLI11 version
VERSION=$(grep '#define CLI11_VERSION "' include/CLI11.hpp | cut -d'"' -f2)
EXPECTED="2.4.2"

if [ "$VERSION" != "$EXPECTED" ]; then
    echo "Error: CLI11 version mismatch. Found: $VERSION, Expected: $EXPECTED"
    exit 1
fi

echo "All dependencies verified (CLI11 v$VERSION)."
