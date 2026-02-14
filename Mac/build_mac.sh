#!/bin/bash

# Ensure we are in the script directory
cd "$(dirname "$0")"

# Symlink source files from parent directory
echo "Linking shared sources..."
ln -sf ../Network.cpp Network.cpp
ln -sf ../Network.h Network.h
ln -sf ../VirtualPrinter.cpp VirtualPrinter.cpp
ln -sf ../VirtualPrinter.h VirtualPrinter.h

# Build for release
echo "Building VirtualESCPOS..."
swift build -c release
BUILD_RESULT=$?

# Restore (remove links)
rm Network.cpp Network.h VirtualPrinter.cpp VirtualPrinter.h

# Check if build was successful
if [ $BUILD_RESULT -eq 0 ]; then
    echo "Build successful."
    
    # Create bin directory if it doesn't exist
    mkdir -p bin
    
    # Define App Bundle structure
    APP_NAME="VirtualESCPOS"
    APP_BUNDLE="dist/$APP_NAME.app"
    CONTENTS_DIR="$APP_BUNDLE/Contents"
    MACOS_DIR="$CONTENTS_DIR/MacOS"
    RESOURCES_DIR="$CONTENTS_DIR/Resources"

    echo "Creating App Bundle: $APP_BUNDLE"
    rm -rf "$APP_BUNDLE"
    mkdir -p "$MACOS_DIR"
    mkdir -p "$RESOURCES_DIR"

    # Copy Info.plist
    # We should have one, let's copy the one created earlier
    cp Source/Info.plist "$CONTENTS_DIR/Info.plist"

    # Copy AppIcon.icns
    if [ -f "Source/AppIcon.icns" ]; then
        cp Source/AppIcon.icns "$RESOURCES_DIR/AppIcon.icns"
    else
        echo "Warning: Mac/AppIcon.icns not found, skipping icon."
    fi

    # Copy binary
    cp .build/release/VirtualESCPOS "$MACOS_DIR/$APP_NAME"

    # Optional: set executable permission just in case
    chmod +x "$MACOS_DIR/$APP_NAME"

    echo "Done! Application bundle is at $APP_BUNDLE"

    # Create a zip file for distribution
    ZIP_NAME="$APP_NAME.mac.zip"
    echo "Creating zip archive: dist/$ZIP_NAME"
    # -r recursive, -y store symlinks as links not referenced file
    # Run in subshell to not change script cwd
    (cd dist && zip -r -y "../dist/$ZIP_NAME" "$APP_NAME.app")
    echo "Zip archive created at dist/$ZIP_NAME"

    # Create a DMG for distribution
    DMG_NAME="dist/$APP_NAME.dmg"
    VOL_NAME="$APP_NAME"
    echo "Creating DMG: $DMG_NAME"
    rm -f "$DMG_NAME"
    hdiutil create -volname "$VOL_NAME" -srcfolder "$APP_BUNDLE" -ov -format UDZO "$DMG_NAME"
    echo "DMG created at $DMG_NAME"
else
    echo "Build failed."
    exit 1
fi
