#!/bin/bash

# Build for release
echo "Building VirtualESCPOS for macOS..."
swift build -c release

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build successful."
    
    # Create bin directory if it doesn't exist
    mkdir -p bin
    
    # Define App Bundle structure
    APP_NAME="VirtualESCPOS"
    APP_BUNDLE="bin/$APP_NAME.app"
    CONTENTS_DIR="$APP_BUNDLE/Contents"
    MACOS_DIR="$CONTENTS_DIR/MacOS"
    RESOURCES_DIR="$CONTENTS_DIR/Resources"

    echo "Creating App Bundle: $APP_BUNDLE"
    rm -rf "$APP_BUNDLE"
    mkdir -p "$MACOS_DIR"
    mkdir -p "$RESOURCES_DIR"

    # Copy Info.plist
    # We should have one, let's copy the one created earlier
    cp Mac/Info.plist "$CONTENTS_DIR/Info.plist"

    # Copy binary
    cp .build/release/VirtualESCPOS "$MACOS_DIR/$APP_NAME"

    # Optional: set executable permission just in case
    chmod +x "$MACOS_DIR/$APP_NAME"

    echo "Done! Application bundle is at $APP_BUNDLE"
else
    echo "Build failed."
    exit 1
fi
