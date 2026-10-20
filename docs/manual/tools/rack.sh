#!/bin/bash
# Runs a private VCV Rack on its own virtual display (:94) to take the manual's screenshots, so it never touches
# your screen or your own Rack. Needs Xvfb, xdotool, ImageMagick and Rack Free in ~/Rack2Free.
#
#   rack.sh setup                 make build/manual-rack (user folder, demo patches, the built plugin)
#   rack.sh start <patch>         start Rack on a patch in build/manual-rack (demo.vcv, panel.vcv, fresh.vcv)
#   rack.sh stop                  stop it (always do this when done)
#   rack.sh shot <name> [geom]    save docs/manual/images/<name>.png, optionally cropped (WxH+X+Y)
#   rack.sh click x y [button] | move x y | drag x1 y1 x2 y2 | key <keys...> | type <text> | modclick <mod> x y
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
D=$REPO/build/manual-rack
U=$D/ru
IMAGES=$REPO/docs/manual/images
W=1600
H=1000
export DISPLAY=:94
[ -e /tmp/.X11-unix/X94 ] || { (Xvfb :94 -screen 0 ${W}x${H}x24 >/dev/null 2>&1 &); sleep 1; }
wid() { xdotool search --pid "$(cat "$D/rpid")" --name "^VCV Rack" 2>/dev/null | head -1; }
case "$1" in
setup)
  mkdir -p "$U/plugins-lin-x64"
  cat > "$U/settings.json" <<EOF
{"windowMaximized": false, "windowSize": [$W, $H], "windowPos": [0, 0], "uiTheme": "dark", "preferDarkPanels": true,
 "showTipsOnLaunch": false, "autoCheckUpdates": false, "tooltips": true, "cpuMeter": false, "frameRateLimit": 60,
 "autosaveInterval": 15, "threadCount": 1, "cableOpacity": 0.6, "cableTension": 0.8, "rackBrightness": 1.0,
 "haloBrightness": 0.25, "cableColors": ["#f3374b", "#ffb437", "#00b56e", "#3695ef", "#8b4ade"]}
EOF
  cp ~/Rack2Free/Fundamental-*.vcvplugin "$U/plugins-lin-x64/" 2>/dev/null
  python3 "$REPO/docs/manual/tools/demo_patch.py" "$D"
  echo "set up $D";;
start)
  [ -f "$D/rpid" ] && kill "$(cat "$D/rpid")" 2>/dev/null; sleep 0.5
  cp "$REPO"/dist/*.vcvplugin "$U/plugins-lin-x64/" && rm -rf "$U/plugins-lin-x64/Tessera"
  rm -rf "$U/autosave" "$U/log.txt"
  cd ~/Rack2Free && (./Rack -u "$U" "$D/$2" > "$D/rack-run.log" 2>&1 &
  echo $! > "$D/rpid")
  for i in $(seq 1 60); do sleep 0.5; w=$(wid); [ -n "$w" ] && break; done
  echo "$w" > "$D/wid"
  sleep 3
  # No window manager: focus must be set by hand, or GLFW ignores key presses.
  xdotool windowmove "$w" 0 0; xdotool windowsize "$w" $W $H; sleep 0.5; xdotool windowfocus --sync "$w"
  echo "window $w";;
stop) kill "$(cat "$D/rpid")" 2>/dev/null; xdotool keyup shift ctrl alt 2>/dev/null; sleep 1;;
shot)
  mkdir -p "$IMAGES"
  if [ -n "$3" ]; then import -window root -crop "$3" +repage "$IMAGES/$2.png"; else import -window root "$IMAGES/$2.png"; fi
  echo "$IMAGES/$2.png";;
click) w=$(cat "$D/wid"); xdotool mousemove --window "$w" "$2" "$3"; sleep 0.25; xdotool mousedown "${4:-1}"; sleep 0.08
  xdotool mouseup "${4:-1}"; sleep 0.6;;
move) w=$(cat "$D/wid"); xdotool mousemove --window "$w" "$2" "$3"; sleep 0.3;;
drag) w=$(cat "$D/wid"); xdotool mousemove --window "$w" "$2" "$3"; sleep 0.25; xdotool mousedown 1; sleep 0.1
  for i in 1 2 3 4 5 6 7 8; do xdotool mousemove --window "$w" $(( $2 + ($4-$2)*i/8 )) $(( $3 + ($5-$3)*i/8 )); sleep 0.05; done
  xdotool mouseup 1; sleep 0.4;;
key) w=$(cat "$D/wid"); shift; xdotool windowfocus --sync "$w"; xdotool key --delay 80 "$@"; sleep 0.4;;
type) w=$(cat "$D/wid"); shift; xdotool windowfocus --sync "$w"; xdotool type --delay 60 "$*"; sleep 0.4;;
modclick) w=$(cat "$D/wid"); xdotool windowfocus --sync "$w"; xdotool mousemove --window "$w" "$3" "$4"; sleep 0.2
  xdotool keydown "$2"; sleep 0.1; xdotool mousedown "${5:-1}"; sleep 0.08; xdotool mouseup "${5:-1}"; sleep 0.1
  xdotool keyup "$2"; sleep 0.6;;
esac
