#!/usr/bin/env bash
# Wrapper script for cppcheck
# Usage:
#   ./tools/cppcheck/run.sh           # console output
#   ./tools/cppcheck/run.sh --html    # HTML report

SCRIPT_DIR=$(dirname "$(realpath "$0")")
ROOT_DIR="$SCRIPT_DIR/../.."

SRC_DIR="$ROOT_DIR/src"
SUPPRESSIONS="$ROOT_DIR/tools/cppcheck/suppressions.txt"
REPORT_XML="$ROOT_DIR/tools/cppcheck/report.xml"
REPORT_HTML_DIR="$ROOT_DIR/tools/cppcheck/html"

if [[ "$1" == "--html" ]]; then
    echo "[*] Running cppcheck (HTML report mode)..."
    cppcheck \
        --enable=all \
        --force \
        --suppressions-list="$SUPPRESSIONS" \
        -I "/usr/include" \
        "$SRC_DIR" \
        --xml \
        --xml-version=2 \
        2> "$REPORT_XML"

    cppcheck-htmlreport \
        --file="$REPORT_XML" \
        --report-dir="$REPORT_HTML_DIR" \
        --source-dir="$SRC_DIR"

    echo "[+] HTML report generated: $REPORT_HTML_DIR/index.html"
else
    echo "[*] Running cppcheck (console mode)..."
    cppcheck \
        --enable=all \
        --quiet \
        --force \
        --suppressions-list="$SUPPRESSIONS" \
        -I "/usr/include" \
        "$SRC_DIR"
fi
