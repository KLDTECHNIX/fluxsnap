#!/bin/sh
set -eu

PROFILE="${1:-default}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
DEST_DIR="$XDG_CONFIG_HOME/fluxsnap"
DEST="$DEST_DIR/config"
EXAMPLES_DIR="${FLUXSNAP_EXAMPLES_DIR:-/usr/local/share/examples/fluxsnap}"

mkdir -p "$DEST_DIR"

case "$PROFILE" in
  default)
    cp "$EXAMPLES_DIR/fluxsnap.conf" "$DEST"
    ;;
  left-main-right-stack)
    cp "$EXAMPLES_DIR/configs/left-main-right-stack.conf" "$DEST"
    ;;
  *)
    echo "unknown profile: $PROFILE" >&2
    echo "valid profiles: default, left-main-right-stack" >&2
    exit 1
    ;;
esac

pkill -x fluxsnap >/dev/null 2>&1 || true
nohup fluxsnap >/dev/null 2>&1 &
