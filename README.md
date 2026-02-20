# fluxsnap

`fluxsnap` is a small X11 helper for Fluxbox that adds Windows 10/11-like edge snapping while dragging windows.

## Features

- Modifier+drag snapping (default: `Super` + left mouse drag)
- Screen darkens as soon as the modifier is held, with a light snap target preview while dragging
- Live window resize while dragging into a snap zone (Windows-like behavior)
- Left/right half snapping
- Corner quarter snapping
- Top edge maximize, plus quick left/right split from top band
- Workarea-aware snapping honors `_NET_WORKAREA` (Fluxbox slit/dock/panels)
- Gap is applied once between adjacent snapped windows (no double middle gap)
- Config file for hotkey/button/threshold/gap tuning

## Build

Install X11 headers/libraries and pkgconf before building:

```sh
doas pkg install libX11 pkgconf
make
```

## Install (FreeBSD local standards)

```sh
doas make install
```

Installs to:

- `/usr/local/bin/fluxsnap`
- `/usr/local/etc/fluxsnap.conf.sample`
- `/usr/local/man/man1/fluxsnap.1`

## Configuration

Copy the sample config to one of:

- `$XDG_CONFIG_HOME/fluxsnap/config`
- `~/.config/fluxsnap/config`
- `/usr/local/etc/fluxsnap.conf`

Example:

```ini
modifier=Super
mouse_button=1
edge_threshold=56
top_band=84
gap=8
```

## Running

```sh
fluxsnap
```

To force a specific config path:

```sh
fluxsnap -c ~/.config/fluxsnap/config
```

For Fluxbox auto-start, add to `~/.fluxbox/startup`:

```sh
fluxsnap &
```

## Preset: Left main + right stacked

A ready-to-use preset for your requested layout is included at:

- `configs/left-main-right-stack.conf`

It creates a workflow where you snap one window to the left half,
then two windows to top-right and bottom-right, with a 30px gap:

```ini
modifier=Ctrl
mouse_button=1
edge_threshold=72
top_band=100
gap=30
```

Run with:

```sh
fluxsnap -c ./configs/left-main-right-stack.conf
```


## Fluxbox integration

`fluxsnap` now ships Fluxbox-ready integration snippets and a profile helper.

Installed files:

- `/usr/local/bin/fluxsnap-profile`
- `/usr/local/share/examples/fluxsnap/fluxbox/menu.inc`
- `/usr/local/share/examples/fluxsnap/fluxbox/keys.sample`
- `/usr/local/share/examples/fluxsnap/fluxbox/init.sample`

Recommended setup:

1. Merge `keys.sample` into `~/.fluxbox/keys` for daemon/profile shortcuts.
2. Merge `menu.inc` into `~/.fluxbox/menu` for menu-driven config/profile actions.
3. Copy the `init.sample` options into `~/.fluxbox/init` to disable Fluxbox native edge snapping conflicts.
4. Run `fluxbox-remote reconfigure`.

Profile switching from Fluxbox keys/menu:

```sh
fluxsnap-profile default
fluxsnap-profile left-main-right-stack
```
