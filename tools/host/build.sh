#!/bin/sh
# Builds and runs the host tools; writes PNG contact sheets to tools/host/out/.
set -e
cd "$(dirname "$0")"
E=../../components/engine
mkdir -p out
SRC="$E/gfx.c $E/font.c $E/mascot_pose.c $E/mascot_anim.c $E/mascot_data.c $E/input.c $E/app.c $E/acts.c $E/room.c $E/toss.c $E/pet_coffee.c $E/ball.c $E/scene_*.c"
cc -O2 -Wall -Wextra -I$E/include preview.c $SRC -lm -o out/preview
cc -O2 -Wall -Wextra -Wno-missing-field-initializers -DHOST_DEBUG -I$E/include scenes.c $SRC -lm -o out/scenes
./out/preview out
./out/scenes out
for f in out/*.bmp; do sips -s format png "$f" --out "${f%.bmp}.png" > /dev/null && rm "$f"; done
