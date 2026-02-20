# fluxsnap

`fluxsnap` is an X11 helper for Fluxbox that does keyboard-triggered smart tiling.

## New workflow

1. Press the configured hotkey (default: `Super+Space`).
2. All open normal windows are tiled evenly in 3 columns with configured gaps.
3. Click any tiled window to open a quick placement menu (`Left`, `Middle`, `Right`).
4. `fluxsnap` remembers that preference and keeps future layouts stacked intelligently.
5. New apps (for example Firefox) are auto-placed and the layout rebalances.

Workarea (`_NET_WORKAREA`) is used, so Fluxbox toolbar/dock reserved space is respected.

## Build

Install X11 headers/libraries and pkgconf before building:

```sh
doas pkg install libX11 pkgconf
make
```

## Install

```sh
doas make install
```

## Configuration

Config lookup order:

- `$XDG_CONFIG_HOME/fluxsnap/config`
- `~/.config/fluxsnap/config`
- `/usr/local/etc/fluxsnap.conf`

Example:

```ini
modifier=Super
hotkey=space
gap=16
```

## Fluxbox integration

Run this once after install:

```sh
fluxsnap-fluxbox-install
```

It merges menu/keys/init snippets into your `~/.fluxbox/*`, reloads Fluxbox, and starts `fluxsnap`.

## Run manually

```sh
fluxsnap
```


## Troubleshooting

If startup fails with `BadAccess` / `X_GrabKey`, that means another program already owns your hotkey.
Pick a different `modifier`/`hotkey` in `~/.config/fluxsnap/config` or unbind the key in Fluxbox.
