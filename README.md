# fluxsnap

`fluxsnap` is a small X11 helper for Fluxbox that adds Windows 10/11-like edge snapping while dragging windows.

## Features

- Modifier+drag snapping (default: `Super` + left mouse drag)
- Live preview rectangle while dragging
- Left/right half snapping
- Corner quarter snapping
- Top edge maximize, plus quick left/right split from top band
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
