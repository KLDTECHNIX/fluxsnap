# Add this block inside ~/.fluxbox/menu
[submenu] (Fluxsnap)
  [exec] (Start Fluxsnap) {pkill -x fluxsnap; fluxsnap &}
  [exec] (Stop Fluxsnap) {pkill -x fluxsnap}
  [exec] (Apply Default Profile) {fluxsnap-profile default}
  [exec] (Apply Left-Main/Right-Stack Profile) {fluxsnap-profile left-main-right-stack}
  [exec] (Edit Fluxsnap Config) {xterm -e sh -lc '${EDITOR:-vi} ~/.config/fluxsnap/config'}
  [exec] (Restart Fluxbox) {fluxbox-remote reconfigure}
[end]
