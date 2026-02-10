#!/bin/bash
set -euo pipefail

FOLDER=vkquake-${VERSION}_linux64
ARCHIVE=$FOLDER.tar.gz

cd /usr/src/vkQuake

# --- Download linuxdeploy tools if not present ---
APPIMAGE_DIR=Packaging/AppImage

LINUXDEPLOY="$APPIMAGE_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"

PLUGIN="$APPIMAGE_DIR/linuxdeploy-plugin-appimage-x86_64.AppImage"
PLUGIN_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-appimage-x86_64.AppImage"
PLUGIN_SHA256="992d502a248e14ab185448ddf6f6e7d25558cb84d4623c354c3af350c25fccb3"

download_if_missing () {
    local file="$1" url="$2" expected_sha="$3"
    if [ ! -f "$file" ]; then
        echo "Downloading $(basename "$file")..."
        wget -q -O "$file" "$url"
        chmod +x "$file"
    fi
    echo "$expected_sha  $file" | sha256sum -c --quiet
}

download_if_missing "$LINUXDEPLOY" "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256"
download_if_missing "$PLUGIN"      "$PLUGIN_URL"      "$PLUGIN_SHA256"

# --- Build ---
rm -rf build/appimage

python3 /opt/meson/meson.py build/appimage -Dbuildtype=release -Db_lto=true -Dmp3_lib=mad
ninja -C build/appimage

cd Packaging/AppImage
rm -rf AppDir
rm -rf vkquake*
mkdir "$FOLDER"
./linuxdeploy-x86_64.AppImage \
	-e ../../build/appimage/vkquake --appdir=AppDir -d ../../Misc/vkquake.desktop \
	-i ../../Misc/vkQuake_256.png --icon-filename=vkquake --output appimage

cp "vkQuake-$VERSION-x86_64.AppImage" "$FOLDER/vkquake.AppImage"
cp ../../LICENSE "$FOLDER"
tar -zcvf "$ARCHIVE" "$FOLDER"
