# fluxsnap

`fluxsnap` is a lightweight X11 tiler for Fluxbox.

## Layout behavior

- Single hotkey trigger (default `Super+Space`).
- Uses EWMH workarea (`_NET_WORKAREA`), so Fluxbox toolbar/slit edges are treated as boundaries.
- Uses a **10px default border** around windows and boundaries.
- Per monitor:
  - **2 windows**: split evenly (2 columns).
  - **3 windows**: split evenly (3 columns).
  - **4+ windows**: start stacking vertically in 3 columns.
- New windows auto-retile on map events.

## Build

```sh
doas pkg install libX11 pkgconf
# optional for strict monitor partitioning
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

Example:

```ini
modifier=Super
hotkey=space
gap=10
```

## Fluxbox integration

```sh
fluxsnap-fluxbox-install
```

## Troubleshooting

If startup reports `BadAccess` / `X_GrabKey`, another program already owns the hotkey.
Change `modifier` / `hotkey` or unbind conflicting Fluxbox keys.
