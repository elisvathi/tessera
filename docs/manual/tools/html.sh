#!/bin/bash
# Builds the user manual as web pages in build/manual-html, to read in a browser. Needs pandoc.
#   docs/manual/tools/html.sh          build, then print the page to open
#   docs/manual/tools/html.sh --open   build and open it in the default browser
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
SRC=$REPO/docs/manual
OUT=$REPO/build/manual-html
command -v pandoc >/dev/null || { echo "pandoc is needed: install it with your package manager"; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$SRC/images" "$OUT/"
cat > "$OUT/style.css" <<'EOF'
:root { --bg: #fdfbf9; --text: #171b21; --muted: #646972; --line: #e1ddd7; --accent: #c45f1c; --code: #f1ede7; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #292b30; --text: #d8dbdf; --muted: #969ca6; --line: #3b3f45; --accent: #ff8a3d; --code: #33363c; }
}
body { background: var(--bg); color: var(--text); font: 16px/1.6 system-ui, sans-serif; margin: 0 auto;
       max-width: 900px; padding: 24px 16px 64px; }
a { color: var(--accent); }
img { max-width: 100%; border-radius: 6px; border: 1px solid var(--line); }
table { border-collapse: collapse; width: 100%; margin: 16px 0; font-size: 15px; }
th, td { border-bottom: 1px solid var(--line); padding: 6px 10px; text-align: left; vertical-align: top; }
code, pre { background: var(--code); border-radius: 4px; padding: 1px 4px; }
pre { padding: 12px; overflow-x: auto; }
blockquote { border-left: 3px solid var(--accent); margin: 16px 0; padding: 2px 16px; color: var(--muted); }
nav.chapters { display: flex; justify-content: space-between; gap: 16px; border-top: 1px solid var(--line);
               margin-top: 48px; padding-top: 16px; }
EOF
chapters=("$SRC"/README.md "$SRC"/[0-9][0-9]-*.md)
for i in "${!chapters[@]}"; do
  md=${chapters[$i]}
  name=$(basename "$md" .md)
  [ "$name" = README ] && name=index
  html() { local n; n=$(basename "$1" .md); [ "$n" = README ] && n=index; echo "$n.html"; }
  nav="<nav class=\"chapters\"><span>"
  [ "$i" -gt 0 ] && nav+="<a href=\"$(html "${chapters[$((i - 1))]}")\">← Previous</a>"
  nav+="</span><a href=\"index.html\">Contents</a><span>"
  [ "$i" -lt $((${#chapters[@]} - 1)) ] && nav+="<a href=\"$(html "${chapters[$((i + 1))]}")\">Next →</a>"
  nav+="</span></nav>"
  title=$(grep -m1 '^# ' "$md" | sed 's/^# //')
  # Links between chapters point at .md files; the pages are .html.
  sed -E 's/\]\(README\.md/](index.html/g; s/\]\(([0-9][0-9]-[a-z-]+)\.md/](\1.html/g' "$md" |
    pandoc -f gfm -t html5 --standalone --css style.css --metadata pagetitle="$title" -A <(echo "$nav") \
      -o "$OUT/$name.html"
done
echo "$OUT/index.html"
[ "$1" = --open ] && xdg-open "$OUT/index.html" >/dev/null 2>&1 &
