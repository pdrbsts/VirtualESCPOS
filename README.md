# VirtualESCPOS

A lightweight C++ Virtual Printer for Windows that simulates an ESC/POS thermal printer. It listens on a TCP port and renders the received raw data in a window, supporting text formatting, bitmaps, and cuts.

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

- **Windows OS**
- **C++ Compiler** (e.g., MSVC `cl.exe`)
- **Git** (for version control)

## Building

To build the project, run the provided batch file:

```cmd
build.bat
```

This will compile the source files and generate `VirtualESCPOS.exe`.

## Usage

1. Run `VirtualESCPOS.exe`.
2. The application will start listening on `localhost:9100`.
3. Send raw ESC/POS data to the port:
   - You can use the provided `test_gs_star.py` for testing.
   - Or send raw binary files using tools like `netcat` or custom scripts.

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests.

## License

This project is open-source.
