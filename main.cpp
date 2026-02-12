#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include "resource.h"
#include <vector>
#include <string>
#include <sstream>
#include "VirtualPrinter.h"
#include "Network.h"

// Registry key path
static const wchar_t* REG_KEY_PATH = L"Software\\MAPENO\\VirtualESCPOS";
static const wchar_t* REG_VAL_PORTA = L"Porto";
static const wchar_t* REG_VAL_COLUNAS = L"Colunas";
static const wchar_t* REG_VAL_WIN_X = L"WinX";
static const wchar_t* REG_VAL_WIN_Y = L"WinY";
static const wchar_t* REG_VAL_WIN_W = L"WinW";
static const wchar_t* REG_VAL_WIN_H = L"WinH";
static const wchar_t* REG_VAL_WIN_MAX = L"WinMax";

#define WM_TRAYICON (WM_USER + 2)
#define ID_TRAY_APP_ICON 1001
#define IDM_RESTORE 1002

void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(nid.szTip, L"MAPENO Impressora Virtual ESC/POS");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// Global variables
VirtualPrinter printer;
NetworkServer server;
HWND hMainWindow;
HINSTANCE hAppInstance;
std::vector<PrinterElement> currentElements;
std::vector<unsigned char> g_rawBuffer;
const size_t MAX_BUFFER_SIZE = 1024 * 1024; // 1MB Limit
float currentY = 10.0f;
float scale = 1.0f; // Zoom factor, maybe?

// Settings
int g_porta = 9100;
int g_colunas = 0;
// Window settings defaults
int g_winX = CW_USEDEFAULT;
int g_winY = CW_USEDEFAULT;
int g_winW = 500;
int g_winH = 700;
bool g_winMax = false;

// ---- Registry helpers ----

void LoadSettings() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwType = REG_DWORD;
        DWORD dwSize = sizeof(DWORD);
        DWORD dwValue;

        if (RegQueryValueEx(hKey, REG_VAL_PORTA, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) {
            g_porta = (int)dwValue;
        }

        dwSize = sizeof(DWORD);
        if (RegQueryValueEx(hKey, REG_VAL_COLUNAS, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) {
            g_colunas = (int)dwValue;
        }
        
        // Window placement
        dwSize = sizeof(DWORD);
        if (RegQueryValueEx(hKey, REG_VAL_WIN_X, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) g_winX = (int)dwValue;
        
        dwSize = sizeof(DWORD);
        if (RegQueryValueEx(hKey, REG_VAL_WIN_Y, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) g_winY = (int)dwValue;

        dwSize = sizeof(DWORD);
        if (RegQueryValueEx(hKey, REG_VAL_WIN_W, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) g_winW = (int)dwValue;

        dwSize = sizeof(DWORD);
        if (RegQueryValueEx(hKey, REG_VAL_WIN_H, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) g_winH = (int)dwValue;

        dwSize = sizeof(DWORD);
        if (RegQueryValueEx(hKey, REG_VAL_WIN_MAX, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) g_winMax = (dwValue != 0);

        RegCloseKey(hKey);
    }
}

void SaveSettings() {
    // Update globals from current window state if window exists
    if (hMainWindow) {
        WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
        if (GetWindowPlacement(hMainWindow, &wp)) {
            g_winX = wp.rcNormalPosition.left;
            g_winY = wp.rcNormalPosition.top;
            g_winW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            g_winH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            g_winMax = (wp.showCmd == SW_SHOWMAXIMIZED);
        }
    }

    HKEY hKey;
    DWORD dwDisposition;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY_PATH, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &dwDisposition) == ERROR_SUCCESS) {
        DWORD dwValue;

        dwValue = (DWORD)g_porta;
        RegSetValueEx(hKey, REG_VAL_PORTA, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));

        dwValue = (DWORD)g_colunas;
        RegSetValueEx(hKey, REG_VAL_COLUNAS, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));
        
        dwValue = (DWORD)g_winX;
        RegSetValueEx(hKey, REG_VAL_WIN_X, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));
        
        dwValue = (DWORD)g_winY;
        RegSetValueEx(hKey, REG_VAL_WIN_Y, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));
        
        dwValue = (DWORD)g_winW;
        RegSetValueEx(hKey, REG_VAL_WIN_W, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));
        
        dwValue = (DWORD)g_winH;
        RegSetValueEx(hKey, REG_VAL_WIN_H, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));
        
        dwValue = g_winMax ? 1 : 0;
        RegSetValueEx(hKey, REG_VAL_WIN_MAX, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));

        RegCloseKey(hKey);
    }
}

// ---- Menu helpers ----

HMENU CreateMainMenu() {
    HMENU hMenu = CreateMenu();
    HMENU hSubMenu = CreatePopupMenu();

    AppendMenu(hSubMenu, MF_STRING, IDM_PORTA, L"&Porto...");
    AppendMenu(hSubMenu, MF_STRING, IDM_COLUNAS, L"&Colunas...");
    AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSubMenu, MF_STRING, IDM_SALVAR, L"&Salvar");
    AppendMenu(hSubMenu, MF_STRING, IDM_SAIR, L"&Sair");

    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"&Menu");

    return hMenu;
}

// ---- Input dialog ----
// Simple modal dialog for entering a numeric value, built at runtime (no .rc template needed)

static int s_dialogValue = 0;
static const wchar_t* s_dialogTitle = L"";

INT_PTR CALLBACK InputDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
    {
        SetWindowText(hDlg, s_dialogTitle);
        HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_VALUE);
        wchar_t buf[32];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", s_dialogValue);
        SetWindowText(hEdit, buf);
        SetFocus(hEdit);
        SendMessage(hEdit, EM_SETSEL, 0, -1);
        return FALSE; // We set focus manually
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_VALUE);
            wchar_t buf[32];
            GetWindowText(hEdit, buf, _countof(buf));
            s_dialogValue = _wtoi(buf);
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// DialogBox using resource
INT_PTR ShowInputDialog(HWND hParent, const wchar_t* title, int currentValue) {
    s_dialogTitle = title;
    s_dialogValue = currentValue;

    INT_PTR result = DialogBox(hAppInstance, MAKEINTRESOURCE(IDD_INPUT_DLG), hParent, InputDlgProc);

    if (result == IDOK) {
        return s_dialogValue;
    }
    return -1; // Cancelled
}


// Update the window title with current port
void UpdateWindowTitle() {
    wchar_t title[128];
    _snwprintf_s(title, _countof(title), _TRUNCATE, L"Impressora ESC/POS Virtual (Porto %d)", g_porta);
    SetWindowText(hMainWindow, title);
}

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
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_COMMAND:
    {
        switch (LOWORD(wParam)) {
        case IDM_PORTA:
        {
            INT_PTR newPort = ShowInputDialog(hwnd, L"Porto TCP", g_porta);
            if (newPort >= 0) {
                int oldPort = g_porta;
                g_porta = (int)newPort;
                SaveSettings();
                UpdateWindowTitle();

                // Restart the server on the new port
                if (g_porta != oldPort) {
                    server.Stop();
                    if (!server.Start(g_porta, [](const unsigned char* data, int len) {
                        printer.ProcessData(data, len);

                        // Buffer data logic
                        if (g_rawBuffer.size() + len > MAX_BUFFER_SIZE) {
                            size_t overflow = (g_rawBuffer.size() + len) - MAX_BUFFER_SIZE;
                            if (overflow < g_rawBuffer.size()) {
                                g_rawBuffer.erase(g_rawBuffer.begin(), g_rawBuffer.begin() + overflow);
                            } else {
                                g_rawBuffer.clear();
                                if ((size_t)len > MAX_BUFFER_SIZE) {
                                     data += (len - MAX_BUFFER_SIZE);
                                     len = MAX_BUFFER_SIZE;
                                }
                            }
                        }
                        g_rawBuffer.insert(g_rawBuffer.end(), data, data + len);
                    })) {
                        wchar_t msg[128];
                        _snwprintf_s(msg, _countof(msg), _TRUNCATE,
                            L"Falha ao iniciar o servidor no porto %d.\nO porto pode estar em uso.", g_porta);
                        MessageBox(hwnd, msg, L"Erro", MB_OK | MB_ICONERROR);
                    }
                }
            }
            return 0;
        }
        case IDM_SALVAR:
        {
            wchar_t filename[MAX_PATH] = L"impressora.txt";
            OPENFILENAME ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"txt";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

            if (GetSaveFileName(&ofn)) {
                HANDLE hFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written;
                    WriteFile(hFile, g_rawBuffer.data(), (DWORD)g_rawBuffer.size(), &written, NULL);
                    CloseHandle(hFile);
                    MessageBox(hwnd, L"Ficheiro guardado com sucesso.", L"Sucesso", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBox(hwnd, L"Erro ao criar ficheiro.", L"Erro", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }
        case IDM_COLUNAS:
        {
            INT_PTR newCols = ShowInputDialog(hwnd, L"Colunas (0 = sem limite)", g_colunas);
            if (newCols >= 0) {
                g_colunas = (int)newCols;
                SaveSettings();
                printer.SetMaxColumns(g_colunas);
            }
            return 0;
        }
        case IDM_SAIR:
            DestroyWindow(hwnd);
            return 0;
        case IDM_RESTORE:
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            return 0;
        }
        break;
    }
    
    case WM_TRAYICON:
    {
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
             ShowWindow(hwnd, SW_RESTORE);
             SetForegroundWindow(hwnd);
        }
        else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, IDM_RESTORE, L"Restaurar");
            AppendMenu(hMenu, MF_STRING, IDM_SAIR, L"Sair");
            
            SetForegroundWindow(hwnd); // Necessary for TrackPopupMenu
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0); // Cleanup
            DestroyMenu(hMenu);
        }
        return 0;
    }

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

    case WM_EXITSIZEMOVE:
        SaveSettings();
        return 0;

    case WM_DESTROY:
        SaveSettings();
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
                    fnWidth = el.isDoubleHeight ? 38 : 19; // Adjusted for visual approximation
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
    hAppInstance = hInstance;

    // Load settings from registry
    LoadSettings();

    const wchar_t CLASS_NAME[] = L"VirtualESCPOSWindow";

    WNDCLASS wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    // Build window title with current port
    wchar_t windowTitle[128];
    _snwprintf_s(windowTitle, _countof(windowTitle), _TRUNCATE, L"Impressora ESC/POS Virtual (Porto %d)", g_porta);

    hMainWindow = CreateWindowEx(
        0, CLASS_NAME, windowTitle,
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        g_winX, g_winY, g_winW, g_winH,
        NULL, NULL, hInstance, NULL
    );

    if (hMainWindow == NULL) {
        return 0;
    }

    // Attach menu
    HMENU hMenu = CreateMainMenu();
    SetMenu(hMainWindow, hMenu);

    ShowWindow(hMainWindow, g_winMax ? SW_SHOWMAXIMIZED : nCmdShow);
    
    // Add Tray Icon
    AddTrayIcon(hMainWindow);
    UpdateWindowTitle(); // Sets tooltip too if I update the function, but for now tooltip is static "Virtual ESC/POS Printer"

    // Setup printer callback
    printer.SetRepaintCallback(UpdatePrinter, NULL);

    // Apply columns setting to the printer
    printer.SetMaxColumns(g_colunas);

    // Start network server on the configured port
    if (!server.Start(g_porta, [](const unsigned char* data, int len) {
        printer.ProcessData(data, len);
        
        // Buffer data logic
        if (g_rawBuffer.size() + len > MAX_BUFFER_SIZE) {
            size_t overflow = (g_rawBuffer.size() + len) - MAX_BUFFER_SIZE;
            if (overflow < g_rawBuffer.size()) {
                g_rawBuffer.erase(g_rawBuffer.begin(), g_rawBuffer.begin() + overflow);
            } else {
                g_rawBuffer.clear();
                if ((size_t)len > MAX_BUFFER_SIZE) {
                        data += (len - MAX_BUFFER_SIZE);
                        len = MAX_BUFFER_SIZE;
                }
            }
        }
        g_rawBuffer.insert(g_rawBuffer.end(), data, data + len);
    })) {
        wchar_t msg[128];
        _snwprintf_s(msg, _countof(msg), _TRUNCATE,
            L"Falha ao iniciar o servidor no porto %d.\nO porto pode estar em uso.", g_porta);
        MessageBox(hMainWindow, msg, L"Erro", MB_OK | MB_ICONERROR);
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
