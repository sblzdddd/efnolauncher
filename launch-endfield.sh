#!/usr/bin/env bash
# DW-Proton / Steam / Lutris wrapper: start CN Endfield via the parent-process bridge.
#
# Why: after 1.6.x, OneChannelLC.dll rejects a direct Endfield.exe start unless the
# direct parent is a signed Games.exe (see LAUNCHER_CHECK.md /
# https://github.com/misaka10843/Hi3Helper.Plugin.Hypergryph/issues/21).
#
# Steam Launch Options:
#   /absolute/path/to/EFNoLauncher/launch-endfield.sh %command%
# Compatibility tool: DW-Proton
#
# Env overrides:
#   ENDFIELD_BRIDGE   path to endfield_bridge.exe (default: beside this script)
#   ENDFIELD_GAME     path to Endfield.exe (Linux or Windows path)
#   ENDFIELD_PARENT   optional signed parent source
#   ENDFIELD_ARGS     extra game args
#   ENDFIELD_DRY_RUN=1  print rewritten command and exit

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BRIDGE_LINUX=${ENDFIELD_BRIDGE:-"$SCRIPT_DIR/endfield_bridge.exe"}
LOG_DIR=${XDG_STATE_HOME:-"$HOME/.local/state"}/endfield-launch
if ! mkdir -p "$LOG_DIR" 2>/dev/null; then
  LOG_DIR="$SCRIPT_DIR"
fi
LOG="$LOG_DIR/launch.log"

log() {
  printf '%s %s\n' "$(date '+%F %T')" "$*" | tee -a "$LOG" >&2
}

die() {
  log "error: $*"
  exit 1
}

wine_prefix() {
  if [[ -n "${WINEPREFIX:-}" ]]; then
    printf '%s\n' "$WINEPREFIX"
  elif [[ -n "${STEAM_COMPAT_DATA_PATH:-}" ]]; then
    printf '%s\n' "$STEAM_COMPAT_DATA_PATH/pfx"
  fi
}

# C:\foo\bar or C:/foo/bar -> $WINEPREFIX/drive_c/foo/bar when possible.
win_to_linux() {
  local p=$1
  case "$p" in
    [A-Za-z]:*|[A-Za-z]:\\*)
      ;;
    *)
      printf '%s\n' "$p"
      return
      ;;
  esac
  local drive rest prefix
  drive=$(printf '%s' "${p:0:1}" | tr '[:upper:]' '[:lower:]')
  rest=${p:2}
  rest=${rest//\\//}
  rest=${rest#/}
  prefix=$(wine_prefix || true)
  if [[ -n "$prefix" && -d "$prefix/drive_$drive" ]]; then
    printf '%s\n' "$prefix/drive_$drive/$rest"
    return
  fi
  if [[ "$drive" == z ]]; then
    printf '/%s\n' "$rest"
    return
  fi
  printf '%s\n' "$p"
}

# Linux absolute path -> Z:\... for Proton/Wine.
linux_to_win() {
  local p
  p=$(readlink -f -- "$1" 2>/dev/null || printf '%s' "$1")
  if [[ "$p" == /* ]]; then
    local win=${p//\//\\}
    printf 'Z:%s\n' "$win"
    return
  fi
  printf '%s\n' "$p"
}

# Works for both Linux paths and Windows paths that still contain backslashes.
basename_ci() {
  local p=$1
  p=${p//\\/\/}
  p=${p%/}
  printf '%s\n' "${p##*/}"
}

is_game_exe() {
  local b
  b=$(basename_ci "$1")
  [[ "$b" == [Ee]ndfield.exe ]]
}

is_launcher_exe() {
  local b
  b=$(basename_ci "$1")
  case "$b" in
    Launcher.exe|Games.exe|HypergryphLauncher*.exe|GRYPHLINK*.exe) return 0 ;;
    *) return 1 ;;
  esac
}

find_endfield_in_args() {
  local arg
  for arg in "$@"; do
    if is_game_exe "$arg"; then
      printf '%s\n' "$arg"
      return 0
    fi
  done
  return 1
}

discover_endfield_near_launcher() {
  local launcher=$1
  local linux_launcher game_dir
  linux_launcher=$(win_to_linux "$launcher")
  [[ -f "$linux_launcher" ]] || return 1
  game_dir=$(dirname -- "$linux_launcher")

  local -a candidates=(
    "$game_dir/Endfield.exe"
    "$game_dir"/games/*/Endfield.exe
    "$game_dir"/games/Endfield*/Endfield.exe
    "$game_dir"/../games/*/Endfield.exe
    "$game_dir"/*/Endfield.exe
  )
  local path
  for path in "${candidates[@]}"; do
    if [[ -f "$path" ]]; then
      printf '%s\n' "$path"
      return 0
    fi
  done
  # Glob may not expand; try find (depth-limited).
  path=$(find "$game_dir" "$game_dir/.." -maxdepth 4 -iname Endfield.exe -type f 2>/dev/null | head -n1 || true)
  [[ -n "$path" && -f "$path" ]] || return 1
  printf '%s\n' "$path"
}

resolve_game_linux() {
  local game_win game_linux arg

  if [[ -n "${ENDFIELD_GAME:-}" ]]; then
    game_linux=$(win_to_linux "$ENDFIELD_GAME")
    [[ -f "$game_linux" ]] || die "ENDFIELD_GAME not found: $ENDFIELD_GAME"
    printf '%s\n' "$game_linux"
    return
  fi

  if game_win=$(find_endfield_in_args "$@"); then
    game_linux=$(win_to_linux "$game_win")
    [[ -f "$game_linux" ]] || die "Endfield.exe from argv not readable: $game_win"
    printf '%s\n' "$game_linux"
    return
  fi

  for arg in "$@"; do
    if is_launcher_exe "$arg"; then
      if game_linux=$(discover_endfield_near_launcher "$arg"); then
        printf '%s\n' "$game_linux"
        return
      fi
    fi
  done

  die "Cannot locate Endfield.exe. Set ENDFIELD_GAME=/path/to/Endfield.exe"
}

build_argv() {
  local game_linux game_win bridge_win parent_win
  game_linux=$(resolve_game_linux "$@")
  game_win=$(linux_to_win "$game_linux")
  [[ -f "$BRIDGE_LINUX" ]] || die "Missing $BRIDGE_LINUX — run: make"
  bridge_win=$(linux_to_win "$BRIDGE_LINUX")

  parent_win=""
  if [[ -n "${ENDFIELD_PARENT:-}" ]]; then
    parent_win=$(linux_to_win "$(win_to_linux "$ENDFIELD_PARENT")")
  fi

  local -a out=()
  local replaced=0 arg
  for arg in "$@"; do
    if [[ $replaced -eq 0 ]] && { is_game_exe "$arg" || is_launcher_exe "$arg"; }; then
      out+=("$bridge_win")
      replaced=1
      continue
    fi
    out+=("$arg")
  done
  if [[ $replaced -eq 0 ]]; then
    out+=("$bridge_win")
  fi

  out+=(--hide-console --no-popup --game "$game_win")
  if [[ -n "$parent_win" ]]; then
    out+=(--parent "$parent_win")
  fi
  if [[ -n "${ENDFIELD_ARGS:-}" ]]; then
    # shellcheck disable=SC2206
    local extra=($ENDFIELD_ARGS)
    out+=(-- "${extra[@]}")
  fi

  REWRITTEN_ARGV=("${out[@]}")
}

main() {
  if [[ $# -eq 0 ]]; then
    cat >&2 <<EOF
usage:
  $0 %command%

Steam → Properties → Launch Options:
  $SCRIPT_DIR/launch-endfield.sh %command%
Compatibility: DW-Proton

Lutris: Wine = dwproton; Executable = $SCRIPT_DIR/endfield_bridge.exe
  or wrap with this script. First build: (cd $SCRIPT_DIR && make)

Env: ENDFIELD_GAME ENDFIELD_PARENT ENDFIELD_BRIDGE ENDFIELD_ARGS ENDFIELD_DRY_RUN=1
EOF
    exit 2
  fi

  log "argv: $*"
  build_argv "$@"
  log "exec: ${REWRITTEN_ARGV[*]}"

  if [[ "${ENDFIELD_DRY_RUN:-0}" == 1 ]]; then
    printf '%s\n' "${REWRITTEN_ARGV[*]}"
    exit 0
  fi
  exec "${REWRITTEN_ARGV[@]}"
}

main "$@"
