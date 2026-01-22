#!/usr/bin/env bash
set -euo pipefail

# Usage: scripts/profile.sh [dataset_path] <iterations> <seed> <bbox> <mode> <duration_seconds>
# Examples:
#   scripts/profile.sh 10000 42 -180,-90,180,90 direct 30
#   scripts/profile.sh /path/to/file.tif 10000 42 -180,-90,180,90 direct 30

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PROFILES_DIR="$ROOT_DIR/profiles"
FLAMEGRAPH_DIR="$HOME/workspace/github/FlameGraph"
GDAL_TEST="$ROOT_DIR/gdal_test"

mkdir -p "$PROFILES_DIR"

# Determine dataset path (optional first argument)
DATASET_DEFAULT="/Users/bopeng/workspace/data/raster/population/ppp_2020_1km_Aggregated.tif"
if [[ $# -ge 6 && -f "$1" ]]; then
  DATASET="$1"; shift
else
  DATASET="$DATASET_DEFAULT"
fi

if [[ $# -lt 5 ]]; then
  echo "Usage: $0 [dataset_path] <iterations> <seed> <bbox> <mode> <duration_seconds>"
  exit 1
fi

ITERS="$1"
SEED="$2"
BBOX="$3"
MODE="$4"
DUR="$5"

# Start the workload
"$GDAL_TEST" "$DATASET" "$ITERS" "$SEED" "$BBOX" "$MODE" &
PID=$!

# Give it a moment to start up
sleep 1

SAMPLE_OUT="$PROFILES_DIR/${MODE}.sample.txt"
FOLDED_OUT="$PROFILES_DIR/${MODE}.folded"
SVG_OUT="$PROFILES_DIR/${MODE}.svg"

# Capture sample profile
/usr/bin/sample "$PID" "$DUR" -file "$SAMPLE_OUT" || true

# Generate flamegraph using stackcollapse-sample.awk + flamegraph.pl
awk -f "$FLAMEGRAPH_DIR/stackcollapse-sample.awk" "$SAMPLE_OUT" > "$FOLDED_OUT" || true
"$FLAMEGRAPH_DIR/flamegraph.pl" "$FOLDED_OUT" > "$SVG_OUT" || true

# Wait for workload to finish
wait "$PID" || true

echo "Generated: $SVG_OUT"
