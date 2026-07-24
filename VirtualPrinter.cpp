#include "VirtualPrinter.h"
#include <iostream>

#include <string>

// Helper for Code Page mapping (Minimal implementation)
// PC437 (Standard) - partial map for common chars
static const wchar_t CP437_Map[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, // 80-87: Ç ü é â ä à å ç
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, // 88-8F: ê ë è ï î ì Ä Å
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, // 90-97: É æ Æ ô ö ò û ù
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, // 98-9F: ÿ Ö Ü ¢ £ ¥ ₧ ƒ
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, // A0-A7: á í ó ú ñ Ñ ª º
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, // A8-AF: ¿ ⌐ ¬ ½ ¼ ¡ « »
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, // B0-B7: ░ ▒ ▓ │ ┤ ╡ ╢ ╖
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, // B8-BF: ╕ ╣ ║ ╗ ╝ ╜ ╛ ┐
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, // C0-C7: └ ┴ ┬ ├ ─ ┼ ╞ ╟
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, // C8-CF: ╚ ╔ ╩ ╦ ╠ ═ ╬ ╧
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, // D0-D7: ╨ ╤ ╥ ╙ ╘ ╒ ╒ ╫
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, // D8-DF: ╪ ┘ ┌ █ ▄ ▌ ▐ ▀
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, // E0-E7: α ß Γ π Σ σ µ τ
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, // E8-EF: Φ Θ Ω δ ∞ φ ε ∩
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, // F0-F7: ≡ ± ≥ ≤ ⌠ ⌡ ÷ ≈
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0  // F8-FF: ° ∙ · √ ⁿ ² ■ NB
};

// PC860 (Portuguese) Map
static const wchar_t CP860_Map[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E3, 0x00E0, 0x00C1, 0x00E7, // 80-87: Ç ü é â ã à Á ç
    0x00EA, 0x00CA, 0x00E8, 0x00CD, 0x00D4, 0x00EC, 0x00C3, 0x00C2, // 88-8F: ê Ê è Í Ô ì Ã Â
    0x00C9, 0x00C0, 0x00C8, 0x00F4, 0x00F5, 0x00F2, 0x00DA, 0x00F9, // 90-97: É À È ô õ ò Ú ù
    0x00CC, 0x00D5, 0x00DC, 0x00A2, 0x00A3, 0x00D9, 0x20A7, 0x00D3, // 98-9F: Ì Õ Ü ¢ £ Ù ₧ Ó
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, // A0-A7: á í ó ú ñ Ñ ª º
    0x00BF, 0x00D2, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, // A8-AF: ¿ Ò ¬ ½ ¼ ¡ « »
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, // B0-B7: ░ ▒ ▓ │ ┤ ╡ ╢ ╖
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, // B8-BF: ╕ ╣ ║ ╗ ╝ ╜ ╛ ┐
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, // C0-C7: └ ┴ ┬ ├ ─ ┼ ╞ ╟
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, // C8-CF: ╚ ╔ ╩ ╦ ╠ ═ ╬ ╧
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, // D0-D7: ╨ ╤ ╥ ╙ ╘ ╒ ╒ ╫
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, // D8-DF: ╪ ┘ ┌ █ ▄ ▌ ▐ ▀
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, // E0-E7: α ß Γ π Σ σ µ τ
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, // E8-EF: Φ Θ Ω δ ∞ φ ε ∩
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, // F0-F7: ≡ ± ≥ ≤ ⌠ ⌡ ÷ ≈
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0  // F8-FF: ° ∙ · √ ⁿ ² ■ NB
};

wchar_t MapChar(unsigned char c, int codePage) {
    if (c < 0x80) return (wchar_t)c;
    int idx = c - 0x80;
    
    // PC860
    if (codePage == 3) {
        return CP860_Map[idx];
    }
    
    // Default PC437
    // Also PC850? Not implemented fully, fallback to 437 usually works for many signs except accents.
    // For now, default to PC437.
    return CP437_Map[idx];
}

VirtualPrinter::VirtualPrinter() {
    state = STATE_NORMAL;
    repaintCallback = nullptr;
    repaintParam = nullptr;
    isRedMode = false;
    isDoubleWidthMode = false;
    isDoubleHeightMode = false;
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
}

VirtualPrinter::~VirtualPrinter() {
    // Cleanup if needed
}

void VirtualPrinter::Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    elements.clear();
    state = STATE_NORMAL;
    isRedMode = false;
    isDoubleWidthMode = false;
    isDoubleHeightMode = false;
    isUnderlineMode = false;
    currentLineSpacing = -1; // Auto
    // Do NOT clear downloaded bitmap on Reset? 
    // Usually printers keep it until power cycle or clear command.
    // We'll keep it.
    currentCodePage = 0; // Default PC437
    currentText = L"";
    currentAlign = 0; // Left
    currentColumn = 0;
    if (repaintCallback) repaintCallback(repaintParam);
}

void VirtualPrinter::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    elements.clear();
    state = STATE_NORMAL;
    isRedMode = false;
    isDoubleWidthMode = false;
    isDoubleHeightMode = false;
    isUnderlineMode = false;
    currentLineSpacing = -1; // Auto
    
    // Clear cache
    downloadedBitmap.clear();
    downloadedBitmapWidthBytes = 0;
    downloadedBitmapHeightBytes = 0;
    downloadedBitmapExpected = 0;

    currentCodePage = 0; // Default PC437
    currentText = L"";
    currentAlign = 0; // Left
    currentColumn = 0;
    if (repaintCallback) repaintCallback(repaintParam);
}

void VirtualPrinter::FlushSegment() {
    if (!currentText.empty()) {
        PrinterElement el;
        el.type = ELEMENT_TEXT;
        el.text = currentText;
        el.isRed = isRedMode;
        el.isDoubleWidth = isDoubleWidthMode;
        el.isDoubleHeight = isDoubleHeightMode;
        el.isUnderline = isUnderlineMode;
        el.align = currentAlign;
        elements.push_back(el);
        currentText = L"";
    }
}

void VirtualPrinter::AddNewLine() {
    FlushSegment();
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
    if (elements.size() >= 2 && elements.back().type == ELEMENT_NEWLINE) {
        PrinterElement& prev = elements[elements.size() - 2];
        if (prev.type == ELEMENT_BITMAP && prev.mergeableBand &&
            !prev.isColumnFormat && prev.width == columns) {
            elements.pop_back(); // drop the inter-band newline
            prev.bitmapData.insert(prev.bitmapData.end(), raster.begin(),
                                   raster.end());
            prev.height += bandHeight;
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
    elements.push_back(el);
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
                else if (b == 0x1B) { // ESC
                    state = STATE_ESC;
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
                         currentText += MapChar(b, currentCodePage);
                         currentColumn++;
                         // Auto-CRLF if maxColumns is set
                         if (maxColumns > 0 && currentColumn >= maxColumns) {
                             AddNewLine();
                         }
                    }
                }
                break;

            case STATE_ESC:
                if (b == 0x40) { // @ Initialize
                    // Reset formatting modes only. On a real printer ESC @ does
                    // NOT erase already-printed paper, so we must not clear
                    // `elements` here: legacy jobs send ESC @ mid-stream (to
                    // reset state before the footer) and clearing would wipe
                    // earlier content such as a QR code. Display separation
                    // between print jobs is handled at the connection level.
                    FlushSegment();
                    isRedMode = false;
                    isDoubleWidthMode = false;
                    isDoubleHeightMode = false;
                    isUnderlineMode = false;
                    currentLineSpacing = -1;
                    currentAlign = 0; // Left
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
                else {
                    state = STATE_NORMAL;
                }
                break;
            
            case STATE_ESC_EXCLAMATION:
                // n parsing
                // Bit 3: Emphasized (Red in our case)
                // Bit 4: Double Height
                // Bit 5: Double Width
                // Bit 7: Underline
                FlushSegment();
                isRedMode = (b & 0x08) != 0;
                isDoubleHeightMode = (b & 0x10) != 0;
                isDoubleWidthMode = (b & 0x20) != 0;
                isUnderlineMode = (b & 0x80) != 0;
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
                if (b == 0x34) { // '4'
                    state = STATE_ESC_c_4;
                }
                else if (b == 0x35) { // '5'
                    state = STATE_ESC_c_5;
                }
                else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_ESC_c_4:
                // Consume n
                state = STATE_NORMAL;
                break;

            case STATE_ESC_c_5:
                // Consume n
                state = STATE_NORMAL;
                break;

            case STATE_ESC_E:
                FlushSegment(); // Flush current text with old style
                if ((b & 1) == 1) {
                    isRedMode = true;
                } else {
                    isRedMode = false;
                }
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
                else {
                    state = STATE_NORMAL;
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
                    elements.push_back(el);
                    
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

                    elements.push_back(el);
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
            }
        }
    }

    // Trigger repaint
    if (repaintCallback) repaintCallback(repaintParam);
}

std::vector<PrinterElement> VirtualPrinter::GetElements() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<PrinterElement> result = elements;

    // Append pending text as a temporary element so it's visible
    if (!currentText.empty()) {
        PrinterElement el;
        el.type = ELEMENT_TEXT;
        el.text = currentText;
        el.isRed = isRedMode;
        el.isDoubleWidth = isDoubleWidthMode;
        el.isDoubleHeight = isDoubleHeightMode;
        el.isUnderline = isUnderlineMode;
        el.align = currentAlign;
        result.push_back(el);
    }

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
