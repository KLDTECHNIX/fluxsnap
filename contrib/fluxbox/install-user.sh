#!/bin/sh
set -eu

XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
FLUXBOX_DIR="${FLUXBOX_DIR:-$HOME/.fluxbox}"
EXAMPLES_DIR="${FLUXSNAP_EXAMPLES_DIR:-/usr/local/share/examples/fluxsnap}"

mkdir -p "$FLUXBOX_DIR"
mkdir -p "$XDG_CONFIG_HOME/fluxsnap"

copy_if_missing() {
  src="$1"
  dst="$2"
  if [ ! -f "$dst" ]; then
    cp "$src" "$dst"
  fi
}

copy_if_missing "$EXAMPLES_DIR/fluxsnap.conf" "$XDG_CONFIG_HOME/fluxsnap/config"
copy_if_missing "$EXAMPLES_DIR/fluxbox/menu.inc" "$FLUXBOX_DIR/menu"
copy_if_missing "$EXAMPLES_DIR/fluxbox/keys.sample" "$FLUXBOX_DIR/keys"
copy_if_missing "$EXAMPLES_DIR/fluxbox/init.sample" "$FLUXBOX_DIR/init"

merge_block() {
  target="$1"
  marker="$2"
  content_file="$3"

  if ! grep -Fq "$marker" "$target" 2>/dev/null; then
    cp "$target" "$target.bak.$(date +%s)"
    {
      echo
      echo "$marker"
      cat "$content_file"
      echo "# END $marker"
    } >> "$target"
  fi
}

merge_block "$FLUXBOX_DIR/menu" "# FLUXSNAP MENU" "$EXAMPLES_DIR/fluxbox/menu.inc"
merge_block "$FLUXBOX_DIR/keys" "# FLUXSNAP KEYS" "$EXAMPLES_DIR/fluxbox/keys.sample"
merge_block "$FLUXBOX_DIR/init" "# FLUXSNAP INIT" "$EXAMPLES_DIR/fluxbox/init.sample"

fluxbox-remote reconfigure >/dev/null 2>&1 || true
pkill -x fluxsnap >/dev/null 2>&1 || true
nohup fluxsnap >/dev/null 2>&1 &

echo "Fluxsnap Fluxbox integration installed into $FLUXBOX_DIR"
echo "Backups were created as *.bak.<timestamp> when files were merged."
