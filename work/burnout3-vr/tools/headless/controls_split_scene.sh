#!/usr/bin/env bash
# controls_split_scene.sh <rundir> <mode> [KEY=VALUE...]
#
# input.control_scheme = ps2 in the two-player stages (src/game/controls.c,
# ctl_driving_for): split_scene.sh's run (Multiplayer > the mode, both pads
# driving; its header has the modes) with the scripted pads standing in for
# two controllers (BO3_CTL_SCRIPTED=1) and the gate's log, then
# controls_split_check.py on that log.
#
#   SCHEME=ps2 (default) | xbox    the layout under test
#   DRIVE_SECS=n                   as split_scene.sh (default 60)
#
# PASS/FAIL per check in <rundir>/controls.txt; exit 1 on a failure. The
# one-player stage is controls_scene.sh.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
rd="$1"; mode="$2"; shift 2
SCHEME="${SCHEME:-ps2}"
REPO="${REPO:-$(cd "$here/../.." && pwd)}"
abs="$(cd "$REPO" && mkdir -p "$rd" && cd "$rd" && pwd)"
rc=0
REPO="$REPO" bash "$here/split_scene.sh" "$rd" "$mode" \
    BO3_CTL_LOG="${GAME_PATH_PREFIX:-}$abs/ctl.txt" BO3_CTL_SCRIPTED=1 \
    RECOMP_INPUT_CONTROL_SCHEME="$SCHEME" "$@" || rc=$?
[ -s "$abs/ctl.txt" ] || { echo "no controls log in $abs" >&2; exit 1; }
python3 "$here/controls_split_check.py" "$SCHEME" "$mode" "$abs/ctl.txt" > "$abs/controls.txt"
crc=$?
cat "$abs/controls.txt"
[ "$rc" = 0 ] && [ "$crc" = 0 ]
