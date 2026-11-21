#!/bin/bash
# Build the Finder/Dock icon with the tools shipped with macOS.
set -eu

if [ "$#" -ne 2 ]; then
    printf 'Usage: %s <source.png> <output.icns>\n' "$0" >&2
    exit 2
fi

source_png=$1
output_icns=$2
if [ ! -f "$source_png" ]; then
    printf 'Application icon does not exist: %s\n' "$source_png" >&2
    exit 1
fi

output_directory=$(dirname "$output_icns")
mkdir -p "$output_directory"
staging_directory=$(mktemp -d "$output_directory/mydict-icon.XXXXXX")
trap 'rm -rf "$staging_directory"' EXIT
iconset="$staging_directory/app-icon.iconset"
mkdir "$iconset"

render_icon() {
    /usr/bin/sips -s format png -z "$1" "$1" "$source_png" \
        --out "$iconset/$2.png" >/dev/null
}

render_icon 16   icon_16x16
render_icon 32   icon_16x16@2x
render_icon 32   icon_32x32
render_icon 64   icon_32x32@2x
render_icon 128  icon_128x128
render_icon 256  icon_128x128@2x
render_icon 256  icon_256x256
render_icon 512  icon_256x256@2x
render_icon 512  icon_512x512
render_icon 1024 icon_512x512@2x

/usr/bin/iconutil --convert icns --output "$staging_directory/app-icon.icns" "$iconset"
# Keep an existing successful icon intact if resizing or conversion fails.
mv -f "$staging_directory/app-icon.icns" "$output_icns"
