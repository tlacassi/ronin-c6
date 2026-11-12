#!/bin/zsh
set -euo pipefail

MACOS_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_FILES=("$MACOS_DIR"/Sources/*.swift)
APP_DIR="$MACOS_DIR/build/ESP Ronin Control.app"
CONTENTS="$APP_DIR/Contents"
SDK_PATH="$(xcrun --show-sdk-path)"
MODULE_CACHE="$MACOS_DIR/build/ModuleCache"

rm -rf "$APP_DIR"
mkdir -p "$CONTENTS/MacOS" "$MODULE_CACHE"
cp "$MACOS_DIR/Info.plist" "$CONTENTS/Info.plist"
python3 "$MACOS_DIR/prepare_resources.py" "$CONTENTS/Resources"

for ARCH in arm64 x86_64; do
	 swiftc \
		-sdk "$SDK_PATH" \
		-module-cache-path "$MODULE_CACHE" \
		-Xcc "-fmodules-cache-path=$MODULE_CACHE" \
		-target "$ARCH-apple-macosx13.0" \
		-swift-version 5 \
		-parse-as-library \
		-O \
		-framework AppKit \
		-framework GameController \
		-framework SwiftUI \
		-framework WebKit \
		"${SOURCE_FILES[@]}" \
		-o "$CONTENTS/MacOS/RoninControl.$ARCH"
done

lipo -create \
	"$CONTENTS/MacOS/RoninControl.arm64" \
	"$CONTENTS/MacOS/RoninControl.x86_64" \
	-output "$CONTENTS/MacOS/RoninControl"
rm "$CONTENTS/MacOS/RoninControl.arm64" "$CONTENTS/MacOS/RoninControl.x86_64"
chmod 755 "$CONTENTS/MacOS/RoninControl"
codesign --force --sign - --timestamp=none "$APP_DIR"

printf 'Built %s\n' "$APP_DIR"
