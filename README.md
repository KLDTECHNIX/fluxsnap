# fluxsnap

`fluxsnap` is a focused X11 tiler for Fluxbox with hotkey-triggered layouting.

## What changed

This version uses a **zone-based layout config** so you can fine tune:

- zone geometry (`x/y/width/height` as percentages)
- per-zone layout mode (`rows`, `cols`, `grid`)
- per-zone max windows (`0` = unlimited)
- per-zone spacing (`zone_gap`)
- global monitor/workarea border (`gap`)

It honors `_NET_WORKAREA`, so Fluxbox toolbar/slit space is treated as boundary.

## Default behavior

- default global border: `10px`
- 3 default zones: left/middle/right
- hotkey: `Super+space`

## Build

```sh
doas pkg install libX11 pkgconf
# optional, for explicit multi-monitor slicing
doas pkg install libXinerama
make
```

## Install

```sh
doas make install
```

## Config

Lookup order:

- `$XDG_CONFIG_HOME/fluxsnap/config`
- `~/.config/fluxsnap/config`
- `/usr/local/etc/fluxsnap.conf`

### Keys

- `modifier` (`Super`, `Alt`, `Ctrl`, `Shift`, etc)
- `hotkey` (X keysym string, e.g. `space`, `F12`, `Return`)
- `gap` (global outer gap)
- `zone` (repeatable)

### Zone line format

```ini
zone=name,x_pct,y_pct,w_pct,h_pct,layout,max_windows,zone_gap
```

Example tuned layout:

```ini
gap=10
zone=left,0,0,50,100,rows,2,10
zone=top_right,50,0,50,50,rows,1,10
zone=bottom_right,50,50,50,50,rows,0,10
```

## Run

```sh
fluxsnap
```

## Recommendation

If Fluxbox native snap is enabled, disable it in `~/.fluxbox/init` to avoid geometry conflicts with `fluxsnap`.
