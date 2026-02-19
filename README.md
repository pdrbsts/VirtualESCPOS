# VirtualESCPOS

A lightweight C++ Virtual Printer for Windows and macOS that simulates an ESC/POS thermal printer. It listens on a TCP port and renders the received raw data in a window, supporting text formatting, bitmaps, and cuts.

## Features

- **TCP Server**: Listens on port `9100` (standard RAW printing port).
- **Text Formatting**: 
  - Emphasized (Bold) / Red mode support.
  - Double Width / Double Height support.
  - Underline support.
  - Custom line spacing (`ESC 3`).
- **Bitmaps**:
  - Raster bit images (`GS v 0`).
  - Downloaded bit images (`GS *` and `GS /`).
- **Paper Handling**:
  - Line feeds and paper cuts (`GS V`).
- **Interactive UI**:
  - Scrollable view of the printed content.
  - Real-time updates as data is received.

## Prerequisites

### Windows
- **Windows OS**
- **C++ Compiler** (e.g., MSVC `cl.exe`)

### macOS
- **macOS** (tested on recent versions)
- **Xcode** or **Swift CLI** (for compiling the Objective-C/Swift bridge)
- **Git** (for version control)

## Building

### Windows
To build the project on Windows, run the provided batch file:

```cmd
build.bat
```

This will compile the source files and generate `bin\VirtualESCPOS.exe`.

### macOS
To build the project on macOS, run the provided shell script:

```bash
chmod +x build_mac.sh
./build_mac.sh
```

This will compile the application and create a Mac App Bundle at `bin/VirtualESCPOS.app`, as well as a zip archive `bin/VirtualESCPOS.mac.zip`.

## Usage

1. Run the application:
   - **Windows**: `bin\VirtualESCPOS.exe`
   - **macOS**: Open `bin/VirtualESCPOS.dmg` (mount it and drag the app to your Applications folder) or unzip `bin/VirtualESCPOS.mac.zip`.
2. The application will start listening on `localhost:9100`.
3. Send raw ESC/POS data to the port:
   - You can use the provided `test_gs_star.py` for testing.
   - Or send raw binary files using tools like `netcat` or custom scripts.

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests.

## License

This project is open-source.
