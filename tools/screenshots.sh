#!/usr/bin/env bash
# Takes the README screenshots. Needs a development build (DEV=1, the default
# of `scripts/ps5.sh`), the demo songs (tools/make-demo-songs.py) in
# /data/playkaraoke/test, FTP on the PS5 and PlayKaraoke freshly opened on the
# home screen. Drives the app with simulated controller input.
set -euo pipefail

HOST="${PS5_HOST:-192.168.31.35}"
OUT="${1:-docs/screenshots}"
TEST=/data/playkaraoke/test
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
mkdir -p "$OUT"

urlenc() { python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$1"; }
api() { curl -fsS -m 10 -X POST "http://$HOST:8000/api/$1" >/dev/null; }
key() { api "dev/input?k=$1"; sleep "${2:-0.8}"; }
text() { api "dev/input?text=$(urlenc "$1")"; sleep 1.5; }
shot() {
  api dev/shot
  curl -fsS -m 30 -o "$OUT/$1.png" "ftp://$HOST:2121/data/playkaraoke/cache/screen.png"
  echo "ok $1"
}
request() { # client name file
  api "queue?client=$1&name=$(urlenc "$2")&path=$(urlenc "$TEST/$3")"
}
phone() { # file query
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars --window-size=500,1000 \
    --force-device-scale-factor=2 --virtual-time-budget=5000 --screenshot="$OUT/$1.png" \
    "http://$HOST:8000/?$2" >/dev/null 2>&1
  echo "ok $1"
}

# Guests requesting from their phones.
request demo-ana Ana "DEMO-001 - The Demo Band - First Song.zip"
request demo-bruno Bruno "DEMO-003 - Sample Singer - Summer Night.mp4"
request demo-carla Carla "DEMO-005 - Karaoke Stars - Dancing Together.zip"
request demo-bruno Bruno "DEMO-004 - Sample Singer - City Lights.zip"
sleep 3.5
shot tv-home

key triangle 2
text "DEMO-00"
key down
key cross 3.5
shot tv-search

key options 1
shot tv-queue
key circle
key circle

key cross 2.5
shot tv-countdown
key cross 9
shot tv-playing
key touch 1
shot tv-controls

phone phone-singing "client=demo-ana&name=Ana"
phone phone-my-turn "client=demo-carla&name=Carla&tab=mine"
phone phone-search "client=demo-bruno&name=Bruno&q=DEMO-00"

key r1 0.3
key r1 0.6
shot tv-key
