# Add this block inside ~/.fluxbox/menu
[submenu] (Fluxsnap)
  [exec] (Start Fluxsnap) {pkill -x fluxsnap; fluxsnap &}
  [exec] (Stop Fluxsnap) {pkill -x fluxsnap}
  [exec] (Edit Fluxsnap Config) {xterm -e sh -lc '${EDITOR:-vi} ~/.config/fluxsnap/config'}
  [exec] (Restart Fluxbox) {fluxbox-remote reconfigure}
[end]
