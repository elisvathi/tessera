#!/bin/bash
# Draws numbered orange markers on a screenshot, so the manual can say "3 is the timeline".
#   annotate.sh <in.png> <out.png> x,y,label [x,y,label ...]
# Example: annotate.sh images/editor-overview.png images/editor-overview-numbered.png 850,19,1 485,19,2
in=$1
out=$2
shift 2
args=()
for spot in "$@"; do
  IFS=, read -r x y label <<< "$spot"
  r=12
  # A one-digit label is about 9 px wide at this size, two digits about 17 px.
  tx=$(( x - (${#label} == 1 ? 4 : 9) ))
  args+=(-fill '#FF8A3D' -stroke white -strokewidth 2 -draw "circle $x,$y $((x + r)),$y")
  args+=(-fill white -stroke none -font DejaVu-Sans-Bold -pointsize 14 -annotate "+$tx+$((y + 5))" "$label")
done
convert "$in" "${args[@]}" "$out"
