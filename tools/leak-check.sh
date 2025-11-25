#!/bin/bash
#
# Script to run valgrind against the test suite for hunting memory leaks
#
# This assumes you run it in the Hammer base directory and have a debug build

HAMMER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VARIANT="${VARIANT:-debug}"
BUILD_PATH="$HAMMER_ROOT/build/$VARIANT"
TEST_SUITE="$BUILD_PATH/src/build/$VARIANT/test_suite"
LIB_PATH="$BUILD_PATH/src"

# Check if test_suite exists
if [ ! -f "$TEST_SUITE" ]; then
    echo "Error: test_suite not found at $TEST_SUITE" >&2
    echo "Please build the test suite first with: scons --variant=$VARIANT test" >&2
    exit 1
fi

# Check if valgrind is available
if ! command -v valgrind >/dev/null 2>&1; then
    echo "Error: valgrind not found. Please install valgrind." >&2
    exit 1
fi

# Set up library path
export LD_LIBRARY_PATH="$LIB_PATH:${LD_LIBRARY_PATH:-}"

VALGRIND=valgrind
VALGRIND_OPTS="-v --leak-check=full --leak-resolution=high --num-callers=40 --partial-loads-ok=no --show-leak-kinds=all --track-origins=yes --undef-value-errors=yes"
VALGRIND_SUPPRESSIONS="valgrind-glib.supp"

for s in $VALGRIND_SUPPRESSIONS
do
  SUPPRESSION_FILE="$HAMMER_ROOT/tools/valgrind/$s"
  if [ -f "$SUPPRESSION_FILE" ]; then
    VALGRIND_OPTS="$VALGRIND_OPTS --suppressions=$SUPPRESSION_FILE"
  else
    echo "Warning: Suppression file not found: $SUPPRESSION_FILE" >&2
  fi
done

echo "Running valgrind on: $TEST_SUITE"
echo "Library path: $LIB_PATH"
echo ""

$VALGRIND $VALGRIND_OPTS "$TEST_SUITE" "$@"
