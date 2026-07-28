#include "VirtualPrinter.h"
#include "Barcode.h"
#include "CodePages.h"
#include "QRCode.h"
#include <iostream>

#include <string>

// ---------------------------------------------------------------------------
// Commands that are recognised but not rendered.
//
// These still have to consume their parameters: the parser returns to
// STATE_NORMAL after the command byte, so any parameter left behind is treated
// as printable text and shows up as garbage on the paper. Each entry is the
// command byte followed by its fixed parameter count.
// ---------------------------------------------------------------------------
struct CmdParams {
    unsigned char cmd;
    int params;
};

static const CmdParams ESC_SKIP[] = {
    {0x25, 1}, // ESC % n  - select/cancel user-defined character set
    {0x34, 0}, // ESC 4    - select italic (non-Epson) / user char set
    {0x35, 0}, // ESC 5    - cancel italic
    {0x3D, 1}, // ESC = n  - select peripheral device
    {0x3F, 1}, // ESC ? n  - cancel user-defined character
    {0x52, 1}, // ESC R n  - select international character set
    {0x70, 3}, // ESC p m t1 t2 - generate cash drawer pulse
    {0x75, 1}, // ESC u n  - transmit peripheral device status
    {0x76, 0}, // ESC v    - transmit paper sensor status
};

static const CmdParams GS_SKIP[] = {
    {0x3A, 0}, // GS :     - start/end macro definition
    {0x49, 1}, // GS I n   - transmit printer ID
    {0x50, 2}, // GS P x y - set motion units
    {0x54, 1}, // GS T n   - move to beginning of print line
    {0x5E, 3}, // GS ^ r t m - execute macro
    {0x61, 1}, // GS a n   - enable/disable automatic status back
    {0x62, 1}, // GS b n   - turn smoothing on/off
    {0x6A, 1}, // GS j n   - enable/disable ASB for ink
    {0x72, 1}, // GS r n   - transmit status
};

static const CmdParams FS_SKIP[] = {
    {0x21, 1}, // FS ! n   - set print mode for Kanji
    {0x26, 0}, // FS &     - select Kanji mode
    {0x2D, 1}, // FS - n   - underline mode for Kanji
    {0x2E, 0}, // FS .     - cancel Kanji mode
    {0x43, 1}, // FS C n   - select Kanji code system
    {0x53, 2}, // FS S n1 n2 - set Kanji character spacing
    {0x57, 1}, // FS W n   - Kanji quadruple size
};

// Upper bound on a single stored image. A malformed stream can claim a huge
// length; past this we consume the bytes without buffering them.
static const long long MAX_IMAGE_BYTES = 8LL * 1024 * 1024;

static int LookupParams(const CmdParams *table, size_t count, unsigned char cmd) {
    for (size_t i = 0; i < count; ++i) {
        if (table[i].cmd == cmd) return table[i].params;
    }
    return -1; // not in the table
}

VirtualPrinter::VirtualPrinter() {
    state = STATE_NORMAL;
    repaintCallback = nullptr;
    repaintParam = nullptr;
    isEmphasizedMode = false;
    isColorRedMode = false;
    widthScaleMode = 1;
    heightScaleMode = 1;
    isReverseMode = false;
    isUpsideDownMode = false;
    isBoldMode = false;
    isRotated90Mode = false;
    charSpacingDots = 0;
    marginLeftDots = 0;
    areaWidthDots = 0;
    currentFont = FONT_A;
    isUnderlineMode = false;
    currentLineSpacing = -1; // Auto
    downloadedBitmapWidthBytes = 0;
    downloadedBitmapHeightBytes = 0;
    downloadedBitmapExpected = 0;
    currentCodePage = 0; // Default PC437
    currentText = L"";
    currentAlign = 0; // Left
    maxColumns = 0;
    currentColumn = 0;
    escStarMode = 0;
    escStarColumns = 0;
    escStarBytesPerColumn = 0;
    escStarBandHeight = 0;
    escStarDataExpected = 0;
    skipRemaining = 0;
    skipReturnState = STATE_NORMAL;
    lenBytesRemaining = 0;
    lenShift = 0;
    pendingLen = 0;
    userCharY = 0;
    userCharRemaining = 0;
    nvImagesRemaining = 0;
    nvHeaderIndex = 0;
    nvHeader[0] = nvHeader[1] = nvHeader[2] = nvHeader[3] = 0;
    nvExpected = 0;
    nvImageIndex = 0;
    barcodeType = -1;
    barcodeHeight = 162; // ESC/POS default
    barcodeModule = 3;   // ESC/POS default
    barcodeHriPos = 0;   // not printed
    barcodeHriFont = 0;
    barcodeExpected = 0;
    parenId = 0;
    parenExpected = 0;
    qrModuleSize = 3;
    qrEcLevel = QR_ECC_LOW;
    pageMode = false;
    pageOriginX = 0;
    pageOriginY = 0;
    pageAreaW = 0;
    pageAreaH = 0;
    pageDirection = 0;
    pageCursorX = 0;
    pageCursorY = 0;
}

VirtualPrinter::~VirtualPrinter() {
    // Cleanup if needed
}

void VirtualPrinter::Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    elements.clear();
    state = STATE_NORMAL;
    isEmphasizedMode = false;
    isColorRedMode = false;
    widthScaleMode = 1;
    heightScaleMode = 1;
    isReverseMode = false;
    isUpsideDownMode = false;
    isBoldMode = false;
    isRotated90Mode = false;
    charSpacingDots = 0;
    marginLeftDots = 0;
    areaWidthDots = 0;
    currentFont = FONT_A;
    isUnderlineMode = false;
    currentLineSpacing = -1; // Auto
    // Do NOT clear downloaded bitmap on Reset? 
    // Usually printers keep it until power cycle or clear command.
    // We'll keep it.
    currentCodePage = 0; // Default PC437
    currentText = L"";
    currentAlign = 0; // Left
    currentColumn = 0;
    tabStops.clear();
    pageMode = false;
    pageOriginX = 0;
    pageOriginY = 0;
    pageAreaW = 0;
    pageAreaH = 0;
    pageDirection = 0;
    pageCursorX = 0;
    pageCursorY = 0;
    pageElements.clear();
    barcodeHeight = 162;
    barcodeModule = 3;
    barcodeHriPos = 0;
    barcodeHriFont = 0;
    barcodeData.clear();
    qrModuleSize = 3;
    qrEcLevel = QR_ECC_LOW;
    qrStoredData.clear();
    if (repaintCallback) repaintCallback(repaintParam);
}

void VirtualPrinter::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    elements.clear();
    state = STATE_NORMAL;
    isEmphasizedMode = false;
    isColorRedMode = false;
    widthScaleMode = 1;
    heightScaleMode = 1;
    isReverseMode = false;
    isUpsideDownMode = false;
    isBoldMode = false;
    isRotated90Mode = false;
    charSpacingDots = 0;
    marginLeftDots = 0;
    areaWidthDots = 0;
    currentFont = FONT_A;
    isUnderlineMode = false;
    currentLineSpacing = -1; // Auto
    
    // Clear cache
    downloadedBitmap.clear();
    downloadedBitmapWidthBytes = 0;
    downloadedBitmapHeightBytes = 0;
    downloadedBitmapExpected = 0;
    nvImages.clear();
    nvBuffer.clear();
    graphicsBuffer = StoredImage();

    currentCodePage = 0; // Default PC437
    currentText = L"";
    currentAlign = 0; // Left
    currentColumn = 0;
    tabStops.clear();
    pageMode = false;
    pageOriginX = 0;
    pageOriginY = 0;
    pageAreaW = 0;
    pageAreaH = 0;
    pageDirection = 0;
    pageCursorX = 0;
    pageCursorY = 0;
    pageElements.clear();
    barcodeHeight = 162;
    barcodeModule = 3;
    barcodeHriPos = 0;
    barcodeHriFont = 0;
    barcodeData.clear();
    qrModuleSize = 3;
    qrEcLevel = QR_ECC_LOW;
    qrStoredData.clear();
    if (repaintCallback) repaintCallback(repaintParam);
}

void VirtualPrinter::ApplyStyle(PrinterElement &el) {
    // ESC E (emphasized) and ESC r (colour) both render red here.
    el.isRed = isEmphasizedMode || isColorRedMode;
    el.widthScale = widthScaleMode;
    el.heightScale = heightScaleMode;
    el.isReverse = isReverseMode;
    el.isUpsideDown = isUpsideDownMode;
    el.isBold = isBoldMode;
    el.isRotated90 = isRotated90Mode;
    el.charSpacing = charSpacingDots;
    el.marginLeft = marginLeftDots;
    el.areaWidth = areaWidthDots;
    el.font = currentFont;
    el.isUnderline = isUnderlineMode;
    el.align = currentAlign;
}

std::vector<PrinterElement>& VirtualPrinter::Target() {
    return pageMode ? pageElements : elements;
}

int VirtualPrinter::CharWidthDots() const {
    int base = (currentFont == FONT_B) ? 10 : (currentFont == FONT_C ? 8 : 12);
    return base * widthScaleMode + charSpacingDots;
}

int VirtualPrinter::CharHeightDots() const {
    // Font B is narrower than Font A but stands in a cell just as tall.
    int base = (currentFont == FONT_C) ? 16 : 24;
    return base * heightScaleMode;
}

int VirtualPrinter::PageFlowLength() const {
    // Directions 1 and 3 print along the short axis of the print area, so the
    // room available for a line of text is the area's height, not its width.
    return (pageDirection == 1 || pageDirection == 3) ? pageAreaH : pageAreaW;
}

void VirtualPrinter::PushElement(PrinterElement& el) {
    if (!pageMode) {
        elements.push_back(el);
        return;
    }
    // In page mode the element keeps the position it was printed at; the print
    // position then moves past it, as it would on a real printer.
    el.pageX = pageCursorX;
    el.pageY = pageCursorY;
    el.pageDir = pageDirection;
    if (el.type == ELEMENT_TEXT) {
        pageCursorX += (int)el.text.length() * CharWidthDots();
    } else if (el.type == ELEMENT_BITMAP) {
        pageCursorX += el.width;
    }
    pageElements.push_back(el);
}

void VirtualPrinter::EnterPageMode() {
    if (pageMode) return; // ESC L in page mode does nothing
    FlushSegment();
    pageMode = true;
    pageElements.clear();
    pageCursorX = 0;
    pageCursorY = 0;
    currentColumn = 0;
}

void VirtualPrinter::LeavePageMode(bool print, bool stayInPageMode) {
    if (!pageMode) return;
    FlushSegment();
    if (print && !pageElements.empty()) {
        PrinterElement begin;
        begin.type = ELEMENT_PAGE_BEGIN;
        begin.pageX = pageOriginX;
        begin.pageY = pageOriginY;
        begin.width = pageAreaW;
        begin.height = pageAreaH;
        elements.push_back(begin);
        elements.insert(elements.end(), pageElements.begin(), pageElements.end());

        PrinterElement end;
        end.type = ELEMENT_PAGE_END;
        end.pageX = pageOriginX;
        end.pageY = pageOriginY;
        end.width = pageAreaW;
        end.height = pageAreaH;
        elements.push_back(end);
    }
    pageElements.clear();
    pageCursorX = 0;
    pageCursorY = 0;
    currentColumn = 0;
    pageMode = stayInPageMode;
}

void VirtualPrinter::FlushSegment() {
    if (!currentText.empty()) {
        PrinterElement el;
        el.type = ELEMENT_TEXT;
        el.text = currentText;
        ApplyStyle(el);
        PushElement(el);
        currentText = L"";
    }
}

void VirtualPrinter::AddSetPos(int dots, bool absolute) {
    FlushSegment();
    if (pageMode) {
        // ESC $ / ESC \ move along the text flow of the current print
        // direction; there is no element to emit, only a cursor move.
        pageCursorX = absolute ? dots : pageCursorX + dots;
        if (pageCursorX < 0) pageCursorX = 0;
        currentColumn = 0;
        return;
    }
    PrinterElement el;
    el.type = ELEMENT_SETPOS;
    ApplyStyle(el);
    el.width = dots;
    el.absolutePos = absolute;
    elements.push_back(el);
}

void VirtualPrinter::AddFeed(int dots) {
    FlushSegment();
    if (pageMode) {
        pageCursorY += dots;
        if (pageCursorY < 0) pageCursorY = 0;
        currentColumn = 0;
        return;
    }
    PrinterElement el;
    el.type = ELEMENT_FEED;
    ApplyStyle(el);
    el.height = dots;
    elements.push_back(el);
    currentColumn = 0;
}

void VirtualPrinter::AddNewLine() {
    FlushSegment();
    if (pageMode) {
        // A line feed in page mode moves down one line inside the print area
        // and back to the start of the line; nothing is printed yet.
        pageCursorY += (currentLineSpacing >= 0) ? currentLineSpacing
                                                 : CharHeightDots();
        pageCursorX = 0;
        currentColumn = 0;
        return;
    }
    PrinterElement el;
    el.type = ELEMENT_NEWLINE;
    // Store current line spacing if fixed, else 0/default
    if (currentLineSpacing >= 0) {
        el.height = currentLineSpacing; // Specific spacing requested
    } else {
        el.height = 0; // Use default auto logic
    }
    elements.push_back(el);
    currentColumn = 0; // Reset column on newline
}

void VirtualPrinter::AddCutLine() {
    // Cutting is not available in page mode; real printers ignore the command.
    if (pageMode) return;
    FlushSegment();
    PrinterElement el;
    el.type = ELEMENT_CUT;
    elements.push_back(el);
}

void VirtualPrinter::CommitEscStarBand() {
    int columns = escStarColumns;
    int bandHeight = escStarBandHeight;
    int bytesPerCol = escStarBytesPerColumn;
    int widthBytes = (columns + 7) / 8;

    // Convert column-major (MSB = top dot) to row-major raster (MSB = left dot),
    // matching the layout expected by the raster renderer (isColumnFormat=false).
    std::vector<unsigned char> raster(widthBytes * bandHeight, 0);
    for (int col = 0; col < columns; ++col) {
        for (int vB = 0; vB < bytesPerCol; ++vB) {
            int srcIdx = col * bytesPerCol + vB;
            if (srcIdx >= (int)escStarData.size()) break;
            unsigned char byte = escStarData[srcIdx];
            for (int bit = 0; bit < 8; ++bit) {
                if ((byte >> (7 - bit)) & 1) {
                    int row = vB * 8 + bit;
                    raster[row * widthBytes + (col / 8)] |= (1 << (7 - (col % 8)));
                }
            }
        }
    }

    // Legacy apps build a tall image (e.g. a QR code) by emitting one band per
    // line, each followed by a line feed. Merge a new band with the immediately
    // preceding band (separated only by that single feed) so the image renders
    // as one contiguous bitmap instead of being sliced by line spacing.
    std::vector<PrinterElement>& target = Target();
    if (!pageMode && target.size() >= 2 &&
        target.back().type == ELEMENT_NEWLINE) {
        PrinterElement& prev = target[target.size() - 2];
        if (prev.type == ELEMENT_BITMAP && prev.mergeableBand &&
            !prev.isColumnFormat && prev.width == columns) {
            target.pop_back(); // drop the inter-band newline
            prev.bitmapData.insert(prev.bitmapData.end(), raster.begin(),
                                   raster.end());
            prev.height += bandHeight;
            return;
        }
    }
    if (pageMode && !target.empty()) {
        // In page mode the bands are not separated by a newline element, so
        // stack them when the next one starts on the line below.
        PrinterElement& prev = target.back();
        if (prev.type == ELEMENT_BITMAP && prev.mergeableBand &&
            !prev.isColumnFormat && prev.width == columns &&
            prev.pageX == 0 && pageCursorX == 0 &&
            prev.pageY + prev.height <= pageCursorY) {
            prev.bitmapData.insert(prev.bitmapData.end(), raster.begin(),
                                   raster.end());
            prev.height += bandHeight;
            pageCursorY = prev.pageY + prev.height;
            return;
        }
    }

    PrinterElement el;
    el.type = ELEMENT_BITMAP;
    el.bitmapData = raster;
    el.width = columns;
    el.height = bandHeight;
    el.isColumnFormat = false; // already converted to raster above
    el.mergeableBand = true;
    el.align = currentAlign; // Justification active when the band began
    PushElement(el);
}

void VirtualPrinter::CommitBarcode() {
    FlushSegment();

    std::vector<bool> dots;
    std::string hri;
    if (!EncodeBarcode(barcodeType, barcodeData, barcodeModule, dots, hri) ||
        dots.empty()) {
        // Real printers print nothing when the data does not fit the
        // symbology, so neither do we.
        barcodeData.clear();
        return;
    }

    // Build a raster bitmap (row-major, MSB = leftmost dot) whose rows are all
    // the same bar pattern; that is the layout the renderer already expects for
    // GS v 0 style bitmaps.
    int width = (int)dots.size();
    int height = (barcodeHeight > 0) ? barcodeHeight : 162;
    int widthBytes = (width + 7) / 8;

    std::vector<unsigned char> row(widthBytes, 0);
    for (int x = 0; x < width; ++x) {
        if (dots[x]) row[x / 8] |= (unsigned char)(1 << (7 - (x % 8)));
    }

    std::vector<unsigned char> raster;
    raster.reserve((size_t)widthBytes * height);
    for (int y = 0; y < height; ++y) {
        raster.insert(raster.end(), row.begin(), row.end());
    }

    // HRI above the bars (GS H n = 1 or 3).
    bool hriAbove = (barcodeHriPos == 1 || barcodeHriPos == 3);
    bool hriBelow = (barcodeHriPos == 2 || barcodeHriPos == 3);

    if (hriAbove && !hri.empty()) {
        currentText.assign(hri.begin(), hri.end());
        FlushSegment();
        AddNewLine();
    }

    PrinterElement el;
    el.type = ELEMENT_BITMAP;
    el.bitmapData = raster;
    el.width = width;
    el.height = height;
    el.isColumnFormat = false;
    el.align = currentAlign;
    PushElement(el);

    if (hriBelow && !hri.empty()) {
        currentText.assign(hri.begin(), hri.end());
        FlushSegment();
        AddNewLine();
    }

    barcodeData.clear();
}

void VirtualPrinter::AddBitmapElement(const std::vector<unsigned char> &raster,
                                      int widthDots, int heightDots) {
    if (raster.empty() || widthDots <= 0 || heightDots <= 0) return;
    FlushSegment();
    PrinterElement el;
    el.type = ELEMENT_BITMAP;
    ApplyStyle(el);
    el.bitmapData = raster;
    el.width = widthDots;
    el.height = heightDots;
    el.isColumnFormat = false;
    PushElement(el);
}

void VirtualPrinter::CommitQRCode() {
    std::vector<std::vector<bool> > matrix;
    if (!EncodeQRCode(qrStoredData, qrEcLevel, matrix) || matrix.empty()) {
        // Too much data for even a version 40 symbol: a real printer prints
        // nothing rather than a partial code.
        return;
    }

    int modules = (int)matrix.size();
    int scale = qrModuleSize > 0 ? qrModuleSize : 3;
    // A quiet zone of 4 modules is part of the symbol; without it scanners
    // that see the bare matrix against neighbouring text often fail.
    const int quiet = 4;
    int sizeModules = modules + quiet * 2;
    int widthDots = sizeModules * scale;
    int widthBytes = (widthDots + 7) / 8;

    std::vector<unsigned char> raster((size_t)widthBytes * widthDots, 0);
    for (int my = 0; my < modules; ++my) {
        for (int mx = 0; mx < modules; ++mx) {
            if (!matrix[my][mx]) continue;
            for (int dy = 0; dy < scale; ++dy) {
                int py = (my + quiet) * scale + dy;
                unsigned char *row = &raster[(size_t)py * widthBytes];
                for (int dx = 0; dx < scale; ++dx) {
                    int px = (mx + quiet) * scale + dx;
                    row[px / 8] |= (unsigned char)(1 << (7 - (px % 8)));
                }
            }
        }
    }

    AddBitmapElement(raster, widthDots, widthDots);
}

void VirtualPrinter::Handle2DCodeCommand() {
    // GS ( k pL pH cn fn [parameters], with cn = 49 for QR Code.
    if (parenData.size() < 2) return;
    int cn = parenData[0];
    int fn = parenData[1];
    if (cn != 49) return; // PDF417 / MaxiCode / other symbologies not drawn

    switch (fn) {
    case 65: // select model - only model 2 is drawn, so nothing to store
        break;
    case 67: // set module size in dots
        if (parenData.size() >= 3) {
            int n = parenData[2];
            if (n >= 1 && n <= 16) qrModuleSize = n;
        }
        break;
    case 69: // set error correction level: '0'..'3' = L, M, Q, H
        if (parenData.size() >= 3) {
            int n = parenData[2] - 48;
            if (n >= 0 && n <= 3) qrEcLevel = n;
        }
        break;
    case 80: // store data in the symbol storage area (parameter m precedes it)
        if (parenData.size() >= 3) {
            qrStoredData.assign(parenData.begin() + 3, parenData.end());
        }
        break;
    case 81: // print the stored symbol
        if (!qrStoredData.empty()) CommitQRCode();
        break;
    case 82: // transmit size information - nothing to draw
        break;
    default:
        break;
    }
}

void VirtualPrinter::PrintStoredImage(const StoredImage &img, int widthScale,
                                      int heightScale) {
    if (img.raster.empty() || img.widthDots <= 0 || img.heightDots <= 0) return;
    if (widthScale < 1) widthScale = 1;
    if (heightScale < 1) heightScale = 1;

    if (widthScale == 1 && heightScale == 1) {
        AddBitmapElement(img.raster, img.widthDots, img.heightDots);
        return;
    }

    int srcBytes = (img.widthDots + 7) / 8;
    int dstWidth = img.widthDots * widthScale;
    int dstHeight = img.heightDots * heightScale;
    int dstBytes = (dstWidth + 7) / 8;
    std::vector<unsigned char> scaled((size_t)dstBytes * dstHeight, 0);

    for (int sy = 0; sy < img.heightDots; ++sy) {
        const unsigned char *srcRow = &img.raster[(size_t)sy * srcBytes];
        for (int sx = 0; sx < img.widthDots; ++sx) {
            if (!((srcRow[sx / 8] >> (7 - (sx % 8))) & 1)) continue;
            for (int ry = 0; ry < heightScale; ++ry) {
                unsigned char *dstRow =
                    &scaled[(size_t)(sy * heightScale + ry) * dstBytes];
                for (int rx = 0; rx < widthScale; ++rx) {
                    int px = sx * widthScale + rx;
                    dstRow[px / 8] |= (unsigned char)(1 << (7 - (px % 8)));
                }
            }
        }
    }
    AddBitmapElement(scaled, dstWidth, dstHeight);
}

void VirtualPrinter::HandleGraphicsCommand() {
    // GS ( L pL pH m fn [parameters] - m is 48 for all the functions we draw.
    if (parenData.size() < 2) return;
    int fn = parenData[1];

    if (fn == 112) {
        // Store raster graphics in the print buffer:
        //   m fn a bx by c xL xH yL yH d1...dk
        if (parenData.size() < 10) return;
        int bx = parenData[3];
        int by = parenData[4];
        int width = parenData[6] + parenData[7] * 256;
        int height = parenData[8] + parenData[9] * 256;
        if (width <= 0 || height <= 0) return;

        size_t widthBytes = (size_t)(width + 7) / 8;
        size_t expected = widthBytes * height;
        if (parenData.size() < 10 + expected) return; // truncated payload

        graphicsBuffer.widthDots = width;
        graphicsBuffer.heightDots = height;
        // The scale factors travel with the buffer until fn 50 prints it.
        graphicsBuffer.scaleX = bx > 0 ? bx : 1;
        graphicsBuffer.scaleY = by > 0 ? by : 1;
        graphicsBuffer.raster.assign(parenData.begin() + 10,
                                     parenData.begin() + 10 + expected);
    } else if (fn == 50 || fn == 2) {
        // Print the graphics currently in the buffer.
        PrintStoredImage(graphicsBuffer, graphicsBuffer.scaleX,
                         graphicsBuffer.scaleY);
    }
}

void VirtualPrinter::HandleParenCommand() {
    if (parenId == 0x6B) { // 'k' - 2D codes
        Handle2DCodeCommand();
    } else if (parenId == 0x4C) { // 'L' - raster graphics
        HandleGraphicsCommand();
    }
    parenData.clear();
}

void VirtualPrinter::StoreNvImage() {
    // FS q stores images column-wise: x bytes across, y bytes down, so the
    // symbol is x*8 dots wide and y*8 tall and each byte holds 8 vertical dots.
    int xBytes = nvHeader[0] + nvHeader[1] * 256;
    int yBytes = nvHeader[2] + nvHeader[3] * 256;
    int widthDots = xBytes * 8;
    int heightDots = yBytes * 8;

    StoredImage img;
    img.widthDots = widthDots;
    img.heightDots = heightDots;
    if (widthDots > 0 && heightDots > 0) {
        int rowBytes = (widthDots + 7) / 8;
        img.raster.assign((size_t)rowBytes * heightDots, 0);
        for (int col = 0; col < widthDots; ++col) {
            for (int vB = 0; vB < yBytes; ++vB) {
                size_t srcIdx = (size_t)col * yBytes + vB;
                if (srcIdx >= nvBuffer.size()) break;
                unsigned char byte = nvBuffer[srcIdx];
                for (int bit = 0; bit < 8; ++bit) {
                    if (!((byte >> (7 - bit)) & 1)) continue;
                    int row = vB * 8 + bit;
                    img.raster[(size_t)row * rowBytes + (col / 8)] |=
                        (unsigned char)(1 << (7 - (col % 8)));
                }
            }
        }
    }
    nvImages.push_back(img);
    nvBuffer.clear();
}

void VirtualPrinter::SkipBytes(long long n, ParseState next) {
    if (n > 0) {
        skipRemaining = n;
        skipReturnState = next;
        state = STATE_SKIP_N;
    } else {
        state = next;
    }
}

void VirtualPrinter::BeginLengthSkip(int numLenBytes) {
    lenBytesRemaining = numLenBytes;
    lenShift = 0;
    pendingLen = 0;
    state = STATE_READ_LEN;
}

void VirtualPrinter::HandleTab() {
    // Advance to the next tab stop. ESC D installs explicit stops; without them
    // printers default to every 8 columns.
    int target = -1;
    for (size_t i = 0; i < tabStops.size(); ++i) {
        if (tabStops[i] > currentColumn) {
            target = tabStops[i];
            break;
        }
    }
    if (target < 0) {
        // Past the last defined stop (or none defined): fall back to every 8.
        target = ((currentColumn / 8) + 1) * 8;
    }
    if (maxColumns > 0 && target >= maxColumns) {
        AddNewLine();
        return;
    }
    while (currentColumn < target) {
        currentText += L' ';
        currentColumn++;
    }
}

void VirtualPrinter::ProcessData(const unsigned char* data, int length) {
    if (length <= 0) return;

    {
        std::lock_guard<std::mutex> lock(mutex);



        for (int i = 0; i < length; ++i) {
            unsigned char b = data[i];


            
            switch (state) {
            case STATE_NORMAL:
                if (b == 0x0A) { // LF
                    AddNewLine();
                }
                else if (b == 0x0D) { // CR
                }
                else if (b == 0x09) { // HT - horizontal tab
                    HandleTab();
                }
                else if (b == 0x0C) { // FF - print the page and leave page mode
                    // In standard mode FF only matters on label printers
                    // (feed to the next black mark), so it is ignored there.
                    LeavePageMode(true);
                }
                else if (b == 0x18) { // CAN - discard the page mode buffer
                    if (pageMode) {
                        currentText = L"";
                        pageElements.clear();
                        pageCursorX = 0;
                        pageCursorY = 0;
                    }
                }
                else if (b == 0x10) { // DLE - real-time commands
                    state = STATE_DLE;
                }
                else if (b == 0x1B) { // ESC
                    state = STATE_ESC;
                }
                else if (b == 0x1C) { // FS - two-byte command set
                    state = STATE_FS;
                }
                else if (b == 0x1D) { // GS
                    state = STATE_GS;
                }
                else {
                    // Printable
                    // Filter out non-printable control codes (0x00-0x1F) that are not handled above
                    // Included handled: 0x0A (LF), 0x0D (CR), 0x1B (ESC), 0x1D (GS)
                    // We should definitely ignore 0x00 (NUL)
                    if (b >= 0x20 || (b > 0x7F && b != 0xFF)) { // 0x80+ are extended chars. 0xFF often ignored?
                         // In page mode a character that would stick out of the
                         // print area moves to the next line inside the area
                         // instead of being printed outside it.
                         int flowLen = pageMode ? PageFlowLength() : 0;
                         if (flowLen > 0) {
                             int used = pageCursorX +
                                 ((int)currentText.length() + 1) * CharWidthDots();
                             if (used > flowLen && (pageCursorX > 0 || !currentText.empty())) {
                                 AddNewLine();
                             }
                         }
                         currentText += MapCodePageChar(b, currentCodePage);
                         currentColumn++;
                         // Auto-CRLF if maxColumns is set (standard mode only:
                         // in page mode the print area does the wrapping)
                         if (!pageMode && maxColumns > 0 && currentColumn >= maxColumns) {
                             AddNewLine();
                         }
                    }
                }
                break;

            case STATE_ESC:
                if (b == 0x40) { // @ Initialize
                    // ESC @ cancels page mode; anything buffered for the page
                    // is discarded, exactly as on a real printer.
                    LeavePageMode(false);
                    pageOriginX = 0;
                    pageOriginY = 0;
                    pageAreaW = 0;
                    pageAreaH = 0;
                    pageDirection = 0;
                    // Reset formatting modes only. On a real printer ESC @ does
                    // NOT erase already-printed paper, so we must not clear
                    // `elements` here: legacy jobs send ESC @ mid-stream (to
                    // reset state before the footer) and clearing would wipe
                    // earlier content such as a QR code. Display separation
                    // between print jobs is handled at the connection level.
                    FlushSegment();
                    isEmphasizedMode = false;
                    isColorRedMode = false;
                    widthScaleMode = 1;
                    heightScaleMode = 1;
                    isReverseMode = false;
                    isUpsideDownMode = false;
                    isBoldMode = false;
                    isRotated90Mode = false;
                    charSpacingDots = 0;
                    marginLeftDots = 0;
                    areaWidthDots = 0;
                    currentFont = FONT_A;
                    isUnderlineMode = false;
                    currentLineSpacing = -1;
                    currentAlign = 0; // Left
                    tabStops.clear();
                    barcodeHeight = 162;
                    barcodeModule = 3;
                    barcodeHriPos = 0;
                    barcodeHriFont = 0;
                    state = STATE_NORMAL;
                }
                else if (b == 0x45) { // E - Emphasized / Red
                    state = STATE_ESC_E; 
                }
                else if (b == 0x2D) { // - - Underline
                    state = STATE_ESC_MINUS;
                }
                else if (b == 0x64) { // d - Print and feed n lines
                    state = STATE_ESC_d;
                }
                else if (b == 0x74) { // t - Select character code table
                    state = STATE_ESC_t;
                }
                else if (b == 0x63) { // c - Button/Sensor commands
                    state = STATE_ESC_c;
                }
                else if (b == 0x33) { // 3 - Set line spacing n
                    state = STATE_ESC_3;
                }
                else if (b == 0x32) { // 2 - Default line spacing
                    // ESC 2 usually sets to approx 1/6 inch (approx 30 dots).
                    currentLineSpacing = 30; 
                    state = STATE_NORMAL;
                }
                else if (b == 0x21) { // ! - Select print mode
                    state = STATE_ESC_EXCLAMATION;
                }
                else if (b == 0x2A) { // * - Select bit image mode (legacy)
                    state = STATE_ESC_STAR;
                }
                else if (b == 0x61) { // a - Select justification
                    state = STATE_ESC_a;
                }
                else if (b == 0x69) { // i - Full cut
                    AddCutLine();
                    state = STATE_NORMAL;
                }
                else if (b == 0x6D) { // m - Partial cut
                    AddCutLine();
                    state = STATE_NORMAL;
                }
                else if (b == 0x28) { // ( - ESC ( fn pL pH d1...dk
                    state = STATE_PAREN_fn;
                }
                else if (b == 0x26) { // & - Define user-defined characters
                    state = STATE_ESC_AMP_y;
                }
                else if (b == 0x44) { // D - Set horizontal tab positions
                    tabStops.clear();
                    state = STATE_ESC_D;
                }
                else if (b == 0x7B) { // { - Upside-down printing
                    state = STATE_ESC_BRACE;
                }
                else if (b == 0x4D) { // M - Select character font
                    state = STATE_ESC_M;
                }
                else if (b == 0x47 || b == 0x67) { // G / g - Double-strike
                    state = STATE_ESC_G;
                }
                else if (b == 0x72) { // r - Select print colour
                    state = STATE_ESC_r;
                }
                else if (b == 0x20) { // SP - Right-side character spacing
                    state = STATE_ESC_SP;
                }
                else if (b == 0x56) { // V - 90 degree clockwise rotation
                    state = STATE_ESC_V;
                }
                else if (b == 0x24) { // $ - Absolute print position
                    state = STATE_ESC_DOLLAR_nL;
                }
                else if (b == 0x5C) { // \ - Relative print position
                    state = STATE_ESC_BSLASH_nL;
                }
                else if (b == 0x4A) { // J - Print and feed n dots
                    state = STATE_ESC_J;
                }
                else if (b == 0x4B) { // K - Print and reverse feed n dots
                    state = STATE_ESC_K;
                }
                else if (b == 0x65) { // e - Print and reverse feed n lines
                    state = STATE_ESC_e;
                }
                else if (b == 0x4C) { // L - Select page mode
                    EnterPageMode();
                    state = STATE_NORMAL;
                }
                else if (b == 0x53) { // S - Select standard mode
                    // Leaving page mode this way throws the page buffer away.
                    LeavePageMode(false);
                    state = STATE_NORMAL;
                }
                else if (b == 0x0C) { // FF - print the page, stay in page mode
                    LeavePageMode(true, true);
                    state = STATE_NORMAL;
                }
                else if (b == 0x54) { // T - Select print direction in page mode
                    state = STATE_ESC_T;
                }
                else if (b == 0x57) { // W - Set print area in page mode
                    pendingParams.clear();
                    state = STATE_ESC_W;
                }
                else {
                    int params = LookupParams(ESC_SKIP,
                                              sizeof(ESC_SKIP) / sizeof(ESC_SKIP[0]), b);
                    if (params >= 0) {
                        SkipBytes(params);
                    } else {
                        state = STATE_NORMAL;
                    }
                }
                break;
            
            case STATE_ESC_EXCLAMATION:
                // n parsing
                // Bit 0: Font B (vs Font A)
                // Bit 3: Emphasized (Red in our case)
                // Bit 4: Double Height
                // Bit 5: Double Width
                // Bit 7: Underline
                FlushSegment();
                currentFont = (b & 0x01) ? FONT_B : FONT_A;
                isEmphasizedMode = (b & 0x08) != 0;
                // ESC ! and GS ! drive the same character-size register, so the
                // last one wins rather than combining.
                heightScaleMode = (b & 0x10) ? 2 : 1;
                widthScaleMode = (b & 0x20) ? 2 : 1;
                isUnderlineMode = (b & 0x80) != 0;
                state = STATE_NORMAL;
                break;

            case STATE_GS_EXCLAMATION:
                // GS ! n - bits 0-2 are the height multiplier - 1,
                //          bits 4-6 the width multiplier - 1 (both 1..8).
                FlushSegment();
                heightScaleMode = (b & 0x07) + 1;
                widthScaleMode = ((b >> 4) & 0x07) + 1;
                state = STATE_NORMAL;
                break;

            case STATE_GS_B:
                // GS B n - the least significant bit turns reverse printing on.
                FlushSegment();
                isReverseMode = (b & 0x01) != 0;
                state = STATE_NORMAL;
                break;

            case STATE_ESC_BRACE:
                // ESC { n - the least significant bit turns upside-down mode on.
                FlushSegment();
                isUpsideDownMode = (b & 0x01) != 0;
                state = STATE_NORMAL;
                break;

            case STATE_ESC_M:
                // ESC M n - 0/48 = Font A, 1/49 = Font B, 2/50 = Font C.
                FlushSegment();
                if (b == 1 || b == 49)      currentFont = FONT_B;
                else if (b == 2 || b == 50) currentFont = FONT_C;
                else                        currentFont = FONT_A;
                state = STATE_NORMAL;
                break;

            case STATE_ESC_G:
                // ESC G n / ESC g n - double-strike, rendered as bold.
                FlushSegment();
                isBoldMode = (b & 0x01) != 0;
                state = STATE_NORMAL;
                break;

            case STATE_ESC_r:
                // ESC r n - 0/48 = black, 1/49 = red.
                FlushSegment();
                isColorRedMode = (b == 1 || b == 49);
                state = STATE_NORMAL;
                break;

            case STATE_ESC_SP:
                // ESC SP n - extra space to the right of each character, in dots.
                FlushSegment();
                charSpacingDots = b;
                state = STATE_NORMAL;
                break;

            case STATE_ESC_V:
                // ESC V n - rotate each character 90 degrees clockwise.
                FlushSegment();
                isRotated90Mode = (b == 1 || b == 49);
                state = STATE_NORMAL;
                break;

            case STATE_ESC_DOLLAR_nL:
                pendingParam = b;
                state = STATE_ESC_DOLLAR_nH;
                break;

            case STATE_ESC_DOLLAR_nH:
                // ESC $ nL nH - absolute position, in dots from the left margin.
                AddSetPos(pendingParam + b * 256, true);
                state = STATE_NORMAL;
                break;

            case STATE_ESC_BSLASH_nL:
                pendingParam = b;
                state = STATE_ESC_BSLASH_nH;
                break;

            case STATE_ESC_BSLASH_nH:
            {
                // ESC \ nL nH - relative move; the 16-bit value is signed, so
                // negative offsets move back towards the left margin.
                int offset = pendingParam + b * 256;
                if (offset > 32767) offset -= 65536;
                AddSetPos(offset, false);
                state = STATE_NORMAL;
                break;
            }

            case STATE_ESC_J:
                // ESC J n - print and feed n dots forward.
                AddFeed(b);
                state = STATE_NORMAL;
                break;

            case STATE_ESC_K:
                // ESC K n - print and feed n dots backwards.
                AddFeed(-(int)b);
                state = STATE_NORMAL;
                break;

            case STATE_ESC_e:
                // ESC e n - print and feed n lines backwards.
                FlushSegment();
                AddFeed(-(int)b * (currentLineSpacing >= 0 ? currentLineSpacing : 30));
                state = STATE_NORMAL;
                break;

            case STATE_ESC_T:
                // ESC T n - print direction in page mode: 0/48 left to right,
                // 1/49 bottom to top, 2/50 right to left, 3/51 top to bottom.
                // Changing direction moves the print position back to the
                // starting corner of the print area.
                FlushSegment();
                if (b >= 48) pageDirection = (b - 48) & 0x03;
                else         pageDirection = b & 0x03;
                pageCursorX = 0;
                pageCursorY = 0;
                currentColumn = 0;
                state = STATE_NORMAL;
                break;

            case STATE_ESC_W:
                // ESC W xL xH yL yH dxL dxH dyL dyH - print area in page mode.
                pendingParams.push_back(b);
                if (pendingParams.size() >= 8) {
                    int x  = pendingParams[0] + pendingParams[1] * 256;
                    int yy = pendingParams[2] + pendingParams[3] * 256;
                    int dx = pendingParams[4] + pendingParams[5] * 256;
                    int dy = pendingParams[6] + pendingParams[7] * 256;
                    pendingParams.clear();
                    // A zero-sized area is an invalid request and is ignored.
                    if (dx > 0 && dy > 0) {
                        FlushSegment();
                        pageOriginX = x;
                        pageOriginY = yy;
                        pageAreaW = dx;
                        pageAreaH = dy;
                        pageCursorX = 0;
                        pageCursorY = 0;
                        currentColumn = 0;
                    }
                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_DOLLAR_nL:
                pendingParam = b;
                state = STATE_GS_DOLLAR_nH;
                break;

            case STATE_GS_DOLLAR_nH:
                // GS $ nL nH - absolute vertical print position inside the page
                // area; it has no effect outside page mode.
                FlushSegment();
                if (pageMode) {
                    pageCursorY = pendingParam + b * 256;
                    currentColumn = 0;
                }
                state = STATE_NORMAL;
                break;

            case STATE_GS_BSLASH_nL:
                pendingParam = b;
                state = STATE_GS_BSLASH_nH;
                break;

            case STATE_GS_BSLASH_nH:
            {
                // GS \ nL nH - relative vertical move; the 16-bit value is
                // signed, so large values move back up the page.
                FlushSegment();
                int offset = pendingParam + b * 256;
                if (offset > 32767) offset -= 65536;
                if (pageMode) {
                    pageCursorY += offset;
                    if (pageCursorY < 0) pageCursorY = 0;
                    currentColumn = 0;
                }
                state = STATE_NORMAL;
                break;
            }

            case STATE_GS_L_nL:
                pendingParam = b;
                state = STATE_GS_L_nH;
                break;

            case STATE_GS_L_nH:
                // GS L nL nH - left margin in dots.
                FlushSegment();
                marginLeftDots = pendingParam + b * 256;
                state = STATE_NORMAL;
                break;

            case STATE_GS_W_nL:
                pendingParam = b;
                state = STATE_GS_W_nH;
                break;

            case STATE_GS_W_nH:
                // GS W nL nH - print area width in dots (0 restores the full
                // paper width).
                FlushSegment();
                areaWidthDots = pendingParam + b * 256;
                state = STATE_NORMAL;
                break;
            
            case STATE_ESC_MINUS:
                // n = 0, 48: Off
                // n = 1, 49: 1-dot width
                // n = 2, 50: 2-dot width
                FlushSegment();
                if (b == 0 || b == 48) {
                    isUnderlineMode = false;
                } else {
                    isUnderlineMode = true;
                }
                state = STATE_NORMAL;
                break;

            case STATE_ESC_d:
                // n lines to feed
                FlushSegment();
                for (int j = 0; j < b; ++j) {
                    AddNewLine();
                }
                state = STATE_NORMAL;
                break;

            case STATE_ESC_3:
                // Set line spacing n
                currentLineSpacing = b;
                state = STATE_NORMAL;
                break;

            case STATE_ESC_a:
                // Select justification: n = 0/'0' left, 1/'1' center, 2/'2' right
                if (b == 1 || b == 49) {
                    currentAlign = 1; // Center
                } else if (b == 2 || b == 50) {
                    currentAlign = 2; // Right
                } else {
                    currentAlign = 0; // Left
                }
                state = STATE_NORMAL;
                break;

            // ESC * m nL nH d1...dk - Select bit image mode.
            // Legacy applications emit graphics (e.g. QR codes) as a series of
            // these bands instead of GS v 0 / GS *. Data is column-major.
            case STATE_ESC_STAR:
                escStarMode = b; // m
                state = STATE_ESC_STAR_nL;
                break;

            case STATE_ESC_STAR_nL:
                escStarColumns = b; // nL
                state = STATE_ESC_STAR_nH;
                break;

            case STATE_ESC_STAR_nH:
                escStarColumns += (b * 256); // + nH*256 = horizontal dots
                // Vertical size depends on the mode:
                //   m = 0, 1  -> 8-dot  density (1 byte per column)
                //   m = 32,33 -> 24-dot density (3 bytes per column)
                escStarBytesPerColumn =
                    (escStarMode == 32 || escStarMode == 33) ? 3 : 1;
                escStarBandHeight = escStarBytesPerColumn * 8;
                escStarDataExpected = escStarColumns * escStarBytesPerColumn;
                if (escStarDataExpected > 0) {
                    FlushSegment(); // Flush text before graphics
                    escStarData.clear();
                    escStarData.reserve(escStarDataExpected);
                    state = STATE_ESC_STAR_DATA;
                } else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_ESC_STAR_DATA:
                escStarData.push_back(b);
                if ((int)escStarData.size() >= escStarDataExpected) {
                    CommitEscStarBand();
                    state = STATE_NORMAL;
                }
                break;

            case STATE_ESC_t:
                // Consume n
                currentCodePage = b;
                state = STATE_NORMAL;
                break;
            
            case STATE_ESC_c:
                // ESC c 0/1 (sheet select), ESC c 3 (paper sensors), ESC c 4
                // (sensors that stop printing), ESC c 5 (panel buttons).
                // All of them take a single parameter byte.
                SkipBytes(1);
                break;

            case STATE_ESC_E:
                FlushSegment(); // Flush current text with old style
                isEmphasizedMode = (b & 1) == 1;
                state = STATE_NORMAL;
                break;

            case STATE_GS:
                if (b == 0x56) { // V Cut
                    state = STATE_GS_V;
                }
                else if (b == 0x76) { // v Raster Bit Image
                    state = STATE_GS_v;
                }
                else if (b == 0x2A) { // * Define download bit image
                    FlushSegment(); // Flush before consuming data
                    state = STATE_GS_STAR;
                }
                else if (b == 0x2F) { // / Print download bit image
                    state = STATE_GS_SLASH;
                }
                else if (b == 0x28) { // ( GS ( <id> pL pH d1...dk
                    state = STATE_GS_PAREN_id;
                }
                else if (b == 0x38) { // 8 GS 8 L p1 p2 p3 p4 ... (large graphics)
                    state = STATE_GS_8;
                }
                else if (b == 0x6B) { // k Print barcode
                    state = STATE_GS_k;
                }
                else if (b == 0x68) { // h Set barcode height
                    state = STATE_GS_h;
                }
                else if (b == 0x77) { // w Set barcode module width
                    state = STATE_GS_w;
                }
                else if (b == 0x48) { // H Select HRI print position
                    state = STATE_GS_H;
                }
                else if (b == 0x66) { // f Select HRI font
                    state = STATE_GS_f;
                }
                else if (b == 0x21) { // ! Select character size
                    state = STATE_GS_EXCLAMATION;
                }
                else if (b == 0x42) { // B Reverse (white on black) printing
                    state = STATE_GS_B;
                }
                else if (b == 0x4C) { // L Set left margin
                    state = STATE_GS_L_nL;
                }
                else if (b == 0x57) { // W Set print area width
                    state = STATE_GS_W_nL;
                }
                else if (b == 0x24) { // $ Absolute vertical position (page mode)
                    state = STATE_GS_DOLLAR_nL;
                }
                else if (b == 0x5C) { // \ Relative vertical position (page mode)
                    state = STATE_GS_BSLASH_nL;
                }
                else {
                    int params = LookupParams(GS_SKIP,
                                              sizeof(GS_SKIP) / sizeof(GS_SKIP[0]), b);
                    if (params >= 0) {
                        SkipBytes(params);
                    } else {
                        state = STATE_NORMAL;
                    }
                }
                break;

            case STATE_GS_V:
                // Function A: GS V m (0,1,48,49) - direct cut
                // Function B: GS V m n (65,66) - feed n lines then cut
                if (b == 65 || b == 66) {
                    state = STATE_GS_V_n; // Wait for n
                } else {
                    // Assume Function A or unknown - just cut
                    AddCutLine();
                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_V_n:
                // Consumed n (feed amount)
                AddCutLine();
                state = STATE_NORMAL;
                break;

            case STATE_GS_v:
                if (b == 0x30) { // '0'
                    state = STATE_GS_v_0;
                }
                else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_v_0:
                bitmapMode = b; // m
                state = STATE_GS_v_0_xL;
                break;

            case STATE_GS_v_0_xL:
                bitmapWidthBytes = b;
                state = STATE_GS_v_0_xH;
                break;

            case STATE_GS_v_0_xH:
                bitmapWidthBytes += (b * 256);
                state = STATE_GS_v_0_yL;
                break;

            case STATE_GS_v_0_yL:
                bitmapHeightDots = b;
                state = STATE_GS_v_0_yH;
                break;

            case STATE_GS_v_0_yH:
                bitmapHeightDots += (b * 256);
                
                // Calculate total bytes expected
                bitmapDataExpected = bitmapWidthBytes * bitmapHeightDots;
                
                if (bitmapDataExpected > 0) {
                    FlushSegment(); // Flush text before bitmap
                    currentBitmapData.clear();
                    currentBitmapData.reserve(bitmapDataExpected);
                    state = STATE_GS_v_0_DATA;
                } else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_v_0_DATA:
                currentBitmapData.push_back(b);
                if (currentBitmapData.size() >= (size_t)bitmapDataExpected) {
                    // All data received
                    PrinterElement el;
                    el.type = ELEMENT_BITMAP;
                    el.bitmapData = currentBitmapData; // Copy
                    el.width = bitmapWidthBytes * 8; // Width in dots
                    el.height = bitmapHeightDots;
                    el.isColumnFormat = false; // Raster format (GS v 0)
                    el.align = currentAlign;
                    PushElement(el);

                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_SLASH:
            {
                // GS / m
                // m values: 0-3, 48-51
                // We should print the downloadedBitmap if m is valid and bitmap exists.
                // Standard: 0=Normal, 1=DoubleWidth, 2=DoubleHeight, 3=Quad.
                FlushSegment(); // Flush preceding text
                
                if (!downloadedBitmap.empty()) {
                    PrinterElement el;
                    el.type = ELEMENT_BITMAP;
                    
                    // Logic to scale? 
                    // ELEMENT_BITMAP supports width/height in dots.
                    // If m requests scaling, we handle it here or in display.
                    // For now, let's just dump it 1:1.
                    
                    el.bitmapData = downloadedBitmap;
                    el.width = downloadedBitmapWidthBytes * 8;
                    el.height = downloadedBitmapHeightBytes * 8; // Yes, * 8. See GS * below.
                    el.isColumnFormat = true; // Column format (GS *)
                    el.align = currentAlign;

                    PushElement(el);
                }
                
                state = STATE_NORMAL;
                break;
            }

            case STATE_GS_STAR:
                downloadedBitmapWidthBytes = b; // x
                state = STATE_GS_STAR_y;
                break;

            case STATE_GS_STAR_y:
                downloadedBitmapHeightBytes = b; // y
                // Calculate expected data size
                // GS * x y d1...dk
                // x is horizontal byte count.
                // y = number of vertical bytes (1 to 48) ??
                
                // Spec says: "Defines a downloaded bit image using x*8 dots in horizontal and y*8 dots in vertical."
                // Data length k = x * y * 8.
                downloadedBitmapExpected = downloadedBitmapWidthBytes * downloadedBitmapHeightBytes * 8;
                
                if (downloadedBitmapExpected > 0) {
                    downloadedBitmap.clear();
                    downloadedBitmap.reserve(downloadedBitmapExpected);
                    state = STATE_GS_STAR_DATA;
                } else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_STAR_DATA:
                downloadedBitmap.push_back(b);
                if (downloadedBitmap.size() >= (size_t)downloadedBitmapExpected) {
                    state = STATE_NORMAL;
                }
                break;

            // --- Generic parameter consumption -----------------------------

            case STATE_SKIP_N:
                if (--skipRemaining <= 0) state = skipReturnState;
                break;

            case STATE_READ_LEN:
                pendingLen |= ((long long)b) << lenShift;
                lenShift += 8;
                if (--lenBytesRemaining <= 0) {
                    SkipBytes(pendingLen);
                }
                break;

            case STATE_PAREN_fn:
                // ESC ( fn pL pH d1...dk - none of these are drawn, so the
                // payload is only consumed.
                BeginLengthSkip(2);
                break;

            case STATE_GS_PAREN_id:
                parenId = b;
                state = STATE_GS_PAREN_pL;
                break;

            case STATE_GS_PAREN_pL:
                pendingParam = b;
                state = STATE_GS_PAREN_pH;
                break;

            case STATE_GS_PAREN_pH:
            {
                parenExpected = pendingParam + b * 256;
                parenData.clear();
                if (parenExpected <= 0) {
                    state = STATE_NORMAL;
                } else if ((parenId == 0x6B || parenId == 0x4C) &&
                           parenExpected <= MAX_IMAGE_BYTES) {
                    // 'k' (2D codes) and 'L' (raster graphics) are drawn, so
                    // their payloads are collected rather than skipped.
                    parenData.reserve((size_t)parenExpected);
                    state = STATE_GS_PAREN_DATA;
                } else {
                    // Other groups are recognised but not drawn; swallow them.
                    SkipBytes(parenExpected);
                }
                break;
            }

            case STATE_GS_PAREN_DATA:
                parenData.push_back(b);
                if ((long long)parenData.size() >= parenExpected) {
                    HandleParenCommand();
                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_8:
                // GS 8 L p1 p2 p3 p4 m fn ... - the identifier is consumed
                // here; the four following bytes are a 32-bit little-endian
                // length. The payload is the same shape as GS ( L, so it is
                // routed through the same handler.
                parenId = b;
                lenBytesRemaining = 4;
                lenShift = 0;
                pendingLen = 0;
                state = STATE_GS_8_LEN;
                break;

            case STATE_GS_8_LEN:
                pendingLen |= ((long long)b) << lenShift;
                lenShift += 8;
                if (--lenBytesRemaining <= 0) {
                    parenExpected = pendingLen;
                    parenData.clear();
                    if (parenExpected <= 0) {
                        state = STATE_NORMAL;
                    } else if (parenId == 0x4C && parenExpected <= MAX_IMAGE_BYTES) {
                        parenData.reserve((size_t)parenExpected);
                        state = STATE_GS_PAREN_DATA;
                    } else {
                        SkipBytes(parenExpected);
                    }
                }
                break;

            case STATE_ESC_AMP_y:
                userCharY = b;
                state = STATE_ESC_AMP_c1;
                break;

            case STATE_ESC_AMP_c1:
                userCharRemaining = b; // holds c1 until c2 arrives
                state = STATE_ESC_AMP_c2;
                break;

            case STATE_ESC_AMP_c2:
                // Characters c1..c2 follow, each as "x d1...d(x*y)".
                userCharRemaining = (int)b - userCharRemaining + 1;
                state = (userCharRemaining > 0) ? STATE_ESC_AMP_x : STATE_NORMAL;
                break;

            case STATE_ESC_AMP_x:
                userCharRemaining--;
                SkipBytes((long long)b * userCharY,
                          userCharRemaining > 0 ? STATE_ESC_AMP_x : STATE_NORMAL);
                break;

            case STATE_ESC_D:
                // ESC D n1...nk NUL - tab stop columns, terminated by NUL.
                if (b == 0x00) {
                    state = STATE_NORMAL;
                } else if (tabStops.size() < 32) {
                    tabStops.push_back(b);
                }
                break;

            case STATE_DLE:
                if (b == 0x04 || b == 0x05) { // DLE EOT n / DLE ENQ n
                    SkipBytes(1);
                } else if (b == 0x14) { // DLE DC4 fn ...
                    state = STATE_DLE_DC4;
                } else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_DLE_DC4:
                // fn = 1: m t (drawer pulse); fn = 2: a b (power off);
                // fn = 8: d1...d7 (clear buffers).
                if (b == 1 || b == 2) {
                    SkipBytes(2);
                } else if (b == 8) {
                    SkipBytes(7);
                } else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_FS:
                if (b == 0x71) { // q - define NV bit images
                    state = STATE_FS_q_n;
                } else if (b == 0x70) { // p - print NV bit image
                    state = STATE_FS_p_n;
                } else {
                    int params = LookupParams(FS_SKIP,
                                              sizeof(FS_SKIP) / sizeof(FS_SKIP[0]), b);
                    if (params >= 0) {
                        SkipBytes(params);
                    } else {
                        state = STATE_NORMAL;
                    }
                }
                break;

            case STATE_FS_q_n:
                // FS q n [xL xH yL yH d1...dk] * n - redefining the NV images
                // replaces whatever was stored before.
                nvImagesRemaining = b;
                nvHeaderIndex = 0;
                nvImages.clear();
                nvBuffer.clear();
                state = (nvImagesRemaining > 0) ? STATE_FS_q_HDR : STATE_NORMAL;
                break;

            case STATE_FS_q_HDR:
                nvHeader[nvHeaderIndex++] = b;
                if (nvHeaderIndex >= 4) {
                    long long xBytes = nvHeader[0] + nvHeader[1] * 256;
                    long long yBytes = nvHeader[2] + nvHeader[3] * 256;
                    nvHeaderIndex = 0;
                    nvExpected = xBytes * yBytes * 8;
                    nvImagesRemaining--;
                    if (nvExpected > 0 && nvExpected <= MAX_IMAGE_BYTES) {
                        nvBuffer.clear();
                        nvBuffer.reserve((size_t)nvExpected);
                        state = STATE_FS_q_DATA;
                    } else {
                        // Empty or implausibly large: consume without storing.
                        SkipBytes(nvExpected, nvImagesRemaining > 0 ? STATE_FS_q_HDR
                                                                   : STATE_NORMAL);
                    }
                }
                break;

            case STATE_FS_q_DATA:
                nvBuffer.push_back(b);
                if ((long long)nvBuffer.size() >= nvExpected) {
                    StoreNvImage();
                    state = (nvImagesRemaining > 0) ? STATE_FS_q_HDR : STATE_NORMAL;
                }
                break;

            case STATE_FS_p_n:
                // FS p n m - n selects the stored image, counting from 1.
                nvImageIndex = b;
                state = STATE_FS_p_m;
                break;

            case STATE_FS_p_m:
            {
                // m: 0/48 normal, 1/49 double width, 2/50 double height,
                // 3/51 quadruple.
                int mode = (b >= 48) ? b - 48 : b;
                int sx = (mode == 1 || mode == 3) ? 2 : 1;
                int sy = (mode == 2 || mode == 3) ? 2 : 1;
                if (nvImageIndex >= 1 && nvImageIndex <= (int)nvImages.size()) {
                    PrintStoredImage(nvImages[nvImageIndex - 1], sx, sy);
                }
                state = STATE_NORMAL;
                break;
            }

            // --- Barcodes ---------------------------------------------------

            case STATE_GS_h: // GS h n - height in dots
                barcodeHeight = b;
                state = STATE_NORMAL;
                break;

            case STATE_GS_w: // GS w n - narrow element width in dots
                // n = 2..6 for the standard symbologies; 68..76 select the
                // wider modules some models offer. Anything else is ignored.
                if (b >= 2 && b <= 6) barcodeModule = b;
                state = STATE_NORMAL;
                break;

            case STATE_GS_H: // GS H n - HRI position
                if (b == 1 || b == 49)      barcodeHriPos = 1; // above
                else if (b == 2 || b == 50) barcodeHriPos = 2; // below
                else if (b == 3 || b == 51) barcodeHriPos = 3; // both
                else                        barcodeHriPos = 0; // not printed
                state = STATE_NORMAL;
                break;

            case STATE_GS_f: // GS f n - HRI font
                barcodeHriFont = b;
                state = STATE_NORMAL;
                break;

            case STATE_GS_k:
                // Function A (m = 0..6) is NUL-terminated; function B
                // (m = 65..73) is preceded by a length byte.
                // An unrecognised m still has its payload consumed: leaking it
                // into the text stream is worse than printing nothing.
                if (b >= 65) {
                    barcodeType = BarcodeTypeFromM(b, false);
                    state = STATE_GS_k_n;
                } else {
                    barcodeType = BarcodeTypeFromM(b, true);
                    barcodeData.clear();
                    state = STATE_GS_k_DATA_A;
                }
                break;

            case STATE_GS_k_DATA_A:
                if (b == 0x00) {
                    CommitBarcode();
                    state = STATE_NORMAL;
                } else {
                    barcodeData.push_back(b);
                }
                break;

            case STATE_GS_k_n:
                barcodeExpected = b;
                barcodeData.clear();
                if (barcodeExpected > 0) {
                    barcodeData.reserve(barcodeExpected);
                    state = STATE_GS_k_DATA_B;
                } else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_GS_k_DATA_B:
                barcodeData.push_back(b);
                if ((int)barcodeData.size() >= barcodeExpected) {
                    CommitBarcode();
                    state = STATE_NORMAL;
                }
                break;
            }
        }
    }

    // Trigger repaint
    if (repaintCallback) repaintCallback(repaintParam);
}

std::vector<PrinterElement> VirtualPrinter::GetElements() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<PrinterElement> result = elements;

    PrinterElement pending;
    bool hasPending = false;
    if (!currentText.empty()) {
        pending.type = ELEMENT_TEXT;
        pending.text = currentText;
        ApplyStyle(pending);
        hasPending = true;
    }

    if (pageMode && (!pageElements.empty() || hasPending)) {
        // The page has not been printed yet (no FF), but showing it as it fills
        // up is more useful than showing nothing at all.
        PrinterElement begin;
        begin.type = ELEMENT_PAGE_BEGIN;
        begin.pageX = pageOriginX;
        begin.pageY = pageOriginY;
        begin.width = pageAreaW;
        begin.height = pageAreaH;
        result.push_back(begin);
        result.insert(result.end(), pageElements.begin(), pageElements.end());
        if (hasPending) {
            pending.pageX = pageCursorX;
            pending.pageY = pageCursorY;
            pending.pageDir = pageDirection;
            result.push_back(pending);
        }
        PrinterElement end;
        end.type = ELEMENT_PAGE_END;
        end.pageX = pageOriginX;
        end.pageY = pageOriginY;
        end.width = pageAreaW;
        end.height = pageAreaH;
        result.push_back(end);
        return result;
    }

    // Append pending text as a temporary element so it's visible
    if (hasPending) result.push_back(pending);

    return result;
}

void VirtualPrinter::SetRepaintCallback(void (*callback)(void*), void* param) {
    repaintCallback = callback;
    repaintParam = param;
}

void VirtualPrinter::SetMaxColumns(int cols) {
    maxColumns = cols;
    currentColumn = 0;
}
