# fluxsnap

`fluxsnap` is an X11 helper for Fluxbox that provides keyboard-triggered smart tiling.

## How it works

1. Press your configured hotkey (default: `Super+Space`).
2. `fluxsnap` tiles all normal windows evenly across 3 columns with gaps.
3. Click any tiled window to choose `Left`, `Middle`, or `Right` from the quick placement menu.
4. That window’s column preference is remembered for future tiling passes.
5. Newly opened windows are automatically tiled and balanced into the layout.

`fluxsnap` uses EWMH `_NET_WORKAREA`, so Fluxbox toolbar/slit/dock reserved space is respected.
When Xinerama is available, layouts are constrained per-monitor so windows stay inside visible monitor bounds.

## Build

Install dependencies on FreeBSD:

```sh
doas pkg install libX11 pkgconf
# optional (recommended for strict multi-monitor bounds):
doas pkg install libXinerama
```

Build:

```sh
make
```

## Install

```sh
doas make install
```

Installed highlights:

- `/usr/local/bin/fluxsnap`
- `/usr/local/bin/fluxsnap-profile`
- `/usr/local/bin/fluxsnap-fluxbox-install`
- `/usr/local/etc/fluxsnap.conf.sample`
- `/usr/local/share/examples/fluxsnap/*`

## Configuration

Lookup order:

- `$XDG_CONFIG_HOME/fluxsnap/config`
- `~/.config/fluxsnap/config`
- `/usr/local/etc/fluxsnap.conf`

Example:

```ini
modifier=Super
hotkey=space
gap=16
```

Supported keys:

- `modifier`: `Super|Mod4`, `Alt|Mod1`, `Ctrl|Control`, `Shift`
- `hotkey`: X keysym string (examples: `space`, `F12`, `Return`)
- `gap`: pixels around and between tiled windows

## Fluxbox integration

Run once after install:

```sh
fluxsnap-fluxbox-install
```

This merges shipped snippets into your user Fluxbox files (`~/.fluxbox/menu`, `~/.fluxbox/keys`, `~/.fluxbox/init`), creates backups, reloads Fluxbox, and starts `fluxsnap`.

## Profiles

Use profile helper to apply a preset config and restart `fluxsnap`:

```sh
fluxsnap-profile default
fluxsnap-profile left-main-right-stack
```

## Run manually

```sh
fluxsnap
```

## Troubleshooting

### `BadAccess` / `X_GrabKey` on startup

Another process already owns your configured hotkey (commonly Fluxbox or a hotkey daemon).

Fix by either:

- changing `modifier` / `hotkey` in `~/.config/fluxsnap/config`, or
- unbinding the conflicting key in Fluxbox / your hotkey daemon.
