#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resource.h"
#include <vector>
#include <string>
#include <sstream>
#include "VirtualPrinter.h"
#include "Network.h"

// Global variables
VirtualPrinter printer;
NetworkServer server;
HWND hMainWindow;
std::vector<PrinterElement> currentElements;
float currentY = 10.0f;
float scale = 1.0f; // Zoom factor, maybe?

// Function to handle repaint
void UpdatePrinter(void* param) {
    // Post message to UI thread to trigger repaint safely
    if (hMainWindow) {
        PostMessage(hMainWindow, WM_USER + 1, 0, 0);
    }
}

void UpdateScroll(HWND hwnd, int totalHeight) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int clientHeight = rect.bottom;

    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = totalHeight + 50; // Add some padding
    si.nPage = clientHeight;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}


// Convert Column-Major format (GS *) to Raster format (Row-Major)
// GS * format: x bytes horizontal implies x*8 columns? Or x bytes width?
// Interpretation: x specifies width in *bytes* (x*8 dots).
//                 y specifies height in *bytes* (y*8 dots).
//                 Data is ordered by COLUMN: Col 0 (y bytes), Col 1 (y bytes)...
//                 Total bytes = (x*8) * y. 
std::vector<unsigned char> ConvertColumnToRaster(const std::vector<unsigned char>& src, int x, int y) {
    int widthDots = x * 8; // Total columns
    int heightDots = y * 8;
    int stride = x; // bytes per row in raster
    std::vector<unsigned char> dst(stride * heightDots, 0); // Zero init (White)

    // Data size check
    // If src size matches x * y * 8, then it is indeed 1 byte per 8 vertical pixels per column.
    
    for (int col = 0; col < widthDots; col++) {
        for (int vB = 0; vB < y; vB++) {
            int srcIdx = col * y + vB;
            if (srcIdx >= src.size()) break;
            
            unsigned char b = src[srcIdx];
            for (int bit = 0; bit < 8; bit++) {
                // MSB is Top for vertical data
                bool isBlack = (b >> (7 - bit)) & 1;
                
                if (isBlack) {
                    int row = vB * 8 + bit;
                    // Set pixel (col, row) in Raster
                    int dstIdx = row * stride + (col / 8);
                    int dstBit = 7 - (col % 8); // MSB Left
                    dst[dstIdx] |= (1 << dstBit);
                }
            }
        }
    }
    return dst;
}

// Convert ESC/POS 1bpp data to GDI compatible 1bpp DIB data (DWORD aligned rows)
std::vector<unsigned char> ConvertToDIB(const std::vector<unsigned char>& src, int widthBytes, int height) {
    int stride = ((widthBytes * 8 + 31) & ~31) / 8;
    std::vector<unsigned char> dib(stride * height);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < widthBytes; x++) {
             if ((y * widthBytes + x) < src.size()) {
                dib[y * stride + x] = src[y * widthBytes + x];
             }
        }
    }
    return dib;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_MOUSEWHEEL:
    {
        int cxDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        // Standard scroll: 120 units = 3 lines? 
        // Let's scroll 20 pixels per "notch" (120) / 3 = 40? 
        // Typically delta is 120. 
        // Let's say one notch = 3 lines of 20px = 60px.
        int scrollAmount = - (cxDelta / WHEEL_DELTA) * 60;

        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int oldPos = si.nPos;
        int newPos = oldPos + scrollAmount;
        
        // Let SetScrollInfo handle clamping
        si.nPos = newPos;
        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd, SB_VERT, &si); 

        if (si.nPos != oldPos) {
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_VSCROLL:
    {
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int oldPos = si.nPos;
        switch (LOWORD(wParam)) {
            case SB_TOP: si.nPos = si.nMin; break;
            case SB_BOTTOM: si.nPos = si.nMax; break;
            case SB_LINEUP: si.nPos -= 20; break;
            case SB_LINEDOWN: si.nPos += 20; break;
            case SB_PAGEUP: si.nPos -= si.nPage; break;
            case SB_PAGEDOWN: si.nPos += si.nPage; break;
            case SB_THUMBTRACK: si.nPos = HIWORD(wParam); break; 
            case SB_THUMBPOSITION: si.nPos = HIWORD(wParam); break; 
        }
        
        if (LOWORD(wParam) == SB_THUMBTRACK || LOWORD(wParam) == SB_THUMBPOSITION) {
             si.nPos = si.nTrackPos;
        }

        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd, SB_VERT, &si); 
        
        if (si.nPos != oldPos) {
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fill background white
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));

        // Get elements thread-safely
        std::vector<PrinterElement> elements = printer.GetElements();

        // Get Scroll Pos
        SCROLLINFO scrollSi;
        scrollSi.cbSize = sizeof(scrollSi);
        scrollSi.fMask = SIF_POS; 
        GetScrollInfo(hwnd, SB_VERT, &scrollSi);
        int yScrollOffset = scrollSi.nPos;

        int y = 10 - yScrollOffset;
        int currentX = 10;
        const int leftMargin = 10;
        int currentLineMaxHeight = 20; // Default line height

        // Create font
        HFONT hFontNormal = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
        
        HGDIOBJ oldFont = SelectObject(hdc, hFontNormal);
        SetBkMode(hdc, TRANSPARENT);
        
        // Create Pen for cut lines
        HPEN hPenCut = CreatePen(PS_DASH, 1, RGB(100, 100, 100));
        HGDIOBJ oldPen = SelectObject(hdc, hPenCut);

        for (const auto& el : elements) {
            if (el.type == ELEMENT_TEXT) {
                // Determine font properties
                int fnHeight = el.isDoubleHeight ? 32 : 16;
                int fnWidth = 0;
                if (el.isDoubleWidth) {
                    // Approximate double width for Courier New
                    // If height is 16, normal width is ~9. Double is ~18.
                    // If height is 32, normal width is ~19. Double is ~38.
                    // Let's use 0 for normal, and explicit for double.
                    fnWidth = el.isDoubleHeight ? 25 : 12; // Adjusted for visual approximation
                }

                HFONT hFontToUse = hFontNormal;
                bool deleteFont = false;

                if (el.isDoubleHeight || el.isDoubleWidth || el.isUnderline) {
                    hFontToUse = CreateFont(fnHeight, fnWidth, 0, 0, FW_NORMAL, FALSE, el.isUnderline, FALSE, 
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
                    deleteFont = true;
                }

                // Track max line height
                // Default font height is 16. Double height is 32.
                // We add some padding (e.g. 4px) to logical height?
                // Visual height is fnHeight.
                if (fnHeight > currentLineMaxHeight) {
                    currentLineMaxHeight = fnHeight;
                }

                SelectObject(hdc, hFontToUse);

                // Determine width
                SIZE size;
                GetTextExtentPoint32(hdc, el.text.c_str(), (int)el.text.length(), &size);
                
                // Set Color
                if (el.isRed) {
                    SetTextColor(hdc, RGB(255, 0, 0));
                } else {
                    SetTextColor(hdc, RGB(0, 0, 0));
                }

                // Adjust Y if double height? 
                // We advance Y based on line height. 
                // If we have mixed height on one line, we should probably align baseline.
                // TextOut aligns to Top-Left by default. 
                // It's tricky with simple TextOut. 
                // Let's assume baseline is at y + 16 (normal)? 
                // Or just draw top-aligned. Largest element defines line height.
                // But we are in a simple flow: "currentX". 
                // If we change font size, "y" is fixed for the current line?
                // Wait, Y is incremented when newline.
                // If we have Mixed content on same line: "Small Big Small"
                // The logical "line" should accommodate the Big one.
                // But my current logic increments Y only on NEWLINE.
                // So all text on this line will share same Y (top).
                // "Big" text will extend downwards. "Small" text will be at top.
                // Ideally we want bottom alignment (baseline).
                // SetTextAlign(hdc, TA_BASELINE) could help? 
                // TA_BASELINE requires us to know where the baseline is.
                // Let's enable TA_BASELINE?
                // Or simpler: Calculate max height of the line? 
                // Too complex for this prompt. Let's stick to Top alignment (TA_TOP default).
                // It will look like top-aligned. It's acceptable for simple sim.
                
                TextOut(hdc, currentX, y, el.text.c_str(), (int)el.text.length());
                currentX += size.cx;

                if (deleteFont) {
                    SelectObject(hdc, oldFont); // Reselect safe default
                    DeleteObject(hFontToUse);
                } else {
                     SelectObject(hdc, hFontNormal); // Restore
                }
            }
            else if (el.type == ELEMENT_NEWLINE) {
                currentX = leftMargin;
                // Add vertical spacing
                // Use explicit spacing if set (el.height).
                if (el.height > 0) {
                     y += el.height;
                } else {
                     y += currentLineMaxHeight + 4; // Use +4 padding as a safe baseline
                }
                currentLineMaxHeight = 20; // Reset to default min
            }
            else if (el.type == ELEMENT_CUT) {
                // Should force newline first just in case?
                if (currentX != leftMargin) {
                    currentX = leftMargin; // Actually just newline
                    y += currentLineMaxHeight + 4;
                    currentLineMaxHeight = 20;
                }
                
                y += 10;
                MoveToEx(hdc, 0, y, NULL);
                LineTo(hdc, rect.right, y);
                // Draw scissors or text "CUT"
                SetTextColor(hdc, RGB(0, 0, 0));
                TextOut(hdc, rect.right - 50, y - 8, L"[CUT]", 5);
                y += 10;
                y += 20; // Advance paper a bit after cut (default line height)
            }
            else if (el.type == ELEMENT_BITMAP) {
                if (currentX != leftMargin) {
                    currentX = leftMargin;
                    y += currentLineMaxHeight + 4;
                    currentLineMaxHeight = 20;
                }

                int w = el.width; // dots
                int h = el.height; // dots
                int wBytes = (w + 7) / 8;

                // GDI Bitmap Info
                struct {
                    BITMAPINFOHEADER bmiHeader;
                    RGBQUAD bmiColors[2];
                } bmi = {0};

                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = w;
                bmi.bmiHeader.biHeight = -h; // Top-down
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 1;
                bmi.bmiHeader.biCompression = BI_RGB;
                
                // Palette: 0 = White, 1 = Black
                bmi.bmiColors[0] = { 255, 255, 255, 0 };
                bmi.bmiColors[1] = { 0, 0, 0, 0 };

                // Handle Column-Format conversion if needed
                std::vector<unsigned char> actualData;
                if (el.isColumnFormat) {
                    // Convert GS * format to Raster
                    int xBytes = w / 8;
                    int yBytes = h / 8;
                    actualData = ConvertColumnToRaster(el.bitmapData, xBytes, yBytes);
                } else {
                    actualData = el.bitmapData;
                }

                // Convert data to padded DIB format
                std::vector<unsigned char> dibData = ConvertToDIB(actualData, wBytes, h);

                SetDIBitsToDevice(hdc, currentX, y, w, h, 
                    0, 0, 0, h, 
                    dibData.data(), (BITMAPINFO*)&bmi, DIB_RGB_COLORS);

                y += h;
                y += 5; // spacing
                currentX = leftMargin;
            }
        }

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldFont);
        DeleteObject(hFontNormal);
        DeleteObject(hPenCut);
        
        // Calculate total logical height
        int totalHeight = y + yScrollOffset; // y is relative, so add offset back
        
        EndPaint(hwnd, &ps);

        // Update Scrollbar
        UpdateScroll(hwnd, totalHeight);
    }
    return 0;

    case WM_USER + 1:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"VirtualESCPOSWindow";

    WNDCLASS wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    hMainWindow = CreateWindowEx(
        0, CLASS_NAME, L"Impressora ESC/POS Virtual (Porto 9100)",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 700,
        NULL, NULL, hInstance, NULL
    );

    if (hMainWindow == NULL) {
        return 0;
    }

    ShowWindow(hMainWindow, nCmdShow);

    // Setup printer callback
    printer.SetRepaintCallback(UpdatePrinter, NULL);

    // Start network server
    if (!server.Start(9100, [](const unsigned char* data, int len) {
        printer.ProcessData(data, len);
    })) {
        MessageBox(hMainWindow, L"Failed to start server on port 9100. Port might be in use.", L"Error", MB_OK | MB_ICONERROR);
    }

    // Main loop
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    server.Stop();
    return 0;
}
