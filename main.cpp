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
static const wchar_t* REG_VAL_FONTE = L"Fonte";
static const wchar_t* REG_VAL_ALWAYSONTOP = L"AlwaysOnTop";

static const wchar_t* STR_INSTALAR_IMPRESSORA = L"Instalar Impressora Virtual";

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
int g_fontSize = 16;
// Window settings defaults
int g_winX = CW_USEDEFAULT;
int g_winY = CW_USEDEFAULT;
int g_winW = 500;
int g_winH = 700;
bool g_winMax = false;
bool g_alwaysOnTop = false;

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

        if (RegQueryValueEx(hKey, REG_VAL_FONTE, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) {
            if ((int)dwValue > 0) g_fontSize = (int)dwValue;
        }

        dwSize = sizeof(DWORD);
        if (RegQueryValueEx(hKey, REG_VAL_ALWAYSONTOP, NULL, &dwType, (LPBYTE)&dwValue, &dwSize) == ERROR_SUCCESS) {
            g_alwaysOnTop = (dwValue != 0);
        }

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

        dwValue = (DWORD)g_fontSize;
        RegSetValueEx(hKey, REG_VAL_FONTE, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));

        dwValue = g_alwaysOnTop ? 1 : 0;
        RegSetValueEx(hKey, REG_VAL_ALWAYSONTOP, 0, REG_DWORD, (LPBYTE)&dwValue, sizeof(DWORD));

        RegCloseKey(hKey);
    }
}

// ---- Menu helpers ----

HMENU CreateMainMenu() {
    HMENU hMenu = CreateMenu();
    HMENU hSubMenu = CreatePopupMenu();

    AppendMenu(hSubMenu, MF_STRING, IDM_PORTA, L"&Porto...");
    AppendMenu(hSubMenu, MF_STRING, IDM_COLUNAS, L"&Colunas...");
    AppendMenu(hSubMenu, MF_STRING, IDM_FONTE, L"&Tamanho do texto...");
    AppendMenu(hSubMenu, MF_STRING | (g_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_ALWAYSONTOP, L"&Sempre no topo");
    AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSubMenu, MF_STRING, IDM_INSTALAR_DRIVER, L"&Instalar Impressora Virtual");
    AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSubMenu, MF_STRING, IDM_LIMPAR, L"&Limpar");
    AppendMenu(hSubMenu, MF_STRING, IDM_SALVAR, L"Salvar");
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

// Glyph height of each ESC/POS font relative to the configured base size:
// Font A is 12x24, Font B 9x17 and Font C smaller still (ESC M n).
static double FontHeightFactor(int font) {
    if (font == FONT_B) return 17.0 / 24.0;
    if (font == FONT_C) return 16.0 / 24.0;
    return 1.0;
}

// Height in pixels of one text element, i.e. the selected font scaled by the
// vertical multiplier from ESC ! / GS !.
static int ElementFontHeight(const PrinterElement& el, int fontSize) {
    int baseHeight = (int)(fontSize * FontHeightFactor(el.font) + 0.5);
    if (baseHeight < 1) baseHeight = 1;
    return baseHeight * el.heightScale;
}

// Converts an ESC/POS horizontal coordinate (dots) into pixels. Text is laid
// out on a character grid, so dots map through the Font A cell width.
static int DotsToPixels(int dots, int charWidth) {
    return MulDiv(dots, charWidth, DOTS_PER_CHAR);
}

// Natural (unscaled) glyph width of an element's font, in pixels.
static int ElementNaturalWidth(const PrinterElement& el, int charWidth) {
    int w = (int)(charWidth * FontHeightFactor(el.font) + 0.5);
    return w < 1 ? 1 : w;
}

// Returns the font to use for a text element, matching the draw path. If a
// special font was created, sets *deleteFont so the caller can DeleteObject it.
static HFONT SelectElementFont(const PrinterElement& el, int fontSize, int charWidth, HFONT hFontNormal, bool* deleteFont) {
    *deleteFont = false;
    if (el.font == FONT_A && el.widthScale == 1 && el.heightScale == 1 &&
        !el.isUnderline && !el.isBold && !el.isRotated90) {
        return hFontNormal;
    }
    int fnHeight = ElementFontHeight(el, fontSize);
    // The width has to be stated explicitly. Left at 0, GDI widens the glyphs
    // in proportion to the height, but ESC/POS scales the two axes separately
    // (GS ! can ask for 1x tall and 4x wide, or the other way round).
    int fnWidth = ElementNaturalWidth(el, charWidth) * el.widthScale;
    // ESC V rotation is done with a world transform at draw time rather than
    // with the font's orientation, because GDI's reference point for rotated
    // text is awkward to place precisely.
    *deleteFont = true;
    return CreateFont(fnHeight, fnWidth, 0, 0,
        el.isBold ? FW_BOLD : FW_NORMAL, FALSE, el.isUnderline, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
}

// Extra pixels inserted after each glyph, from ESC SP.
static int ElementCharExtra(const PrinterElement& el, int charWidth) {
    return DotsToPixels(el.charSpacing, charWidth);
}

// Width of one cell of an ESC V rotated run: a glyph turned 90 degrees is as
// wide as the font is tall.
static int RotatedCellWidth(const PrinterElement& el, int fontSize, int charWidth) {
    int cell = ElementFontHeight(el, fontSize) + DotsToPixels(el.charSpacing, charWidth);
    return cell < 1 ? 1 : cell;
}

// Total pixel width of the run of TEXT elements starting at startIdx, up to the
// next line break (NEWLINE / CUT / BITMAP) or the end of the list. Used to
// compute the horizontal offset for centered/right justification (ESC a).
static int MeasureTextLineWidth(HDC hdc, const std::vector<PrinterElement>& elements, size_t startIdx, int fontSize, int charWidth, HFONT hFontNormal) {
    int total = 0;
    for (size_t i = startIdx; i < elements.size(); ++i) {
        if (elements[i].type != ELEMENT_TEXT) break;
        if (elements[i].isRotated90) {
            total += RotatedCellWidth(elements[i], fontSize, charWidth) *
                     (int)elements[i].text.length();
            continue;
        }
        bool del = false;
        HFONT f = SelectElementFont(elements[i], fontSize, charWidth, hFontNormal, &del);
        HGDIOBJ old = SelectObject(hdc, f);
        SetTextCharacterExtra(hdc, ElementCharExtra(elements[i], charWidth));
        SIZE size;
        GetTextExtentPoint32(hdc, elements[i].text.c_str(), (int)elements[i].text.length(), &size);
        total += size.cx;
        SetTextCharacterExtra(hdc, 0);
        SelectObject(hdc, old);
        if (del) DeleteObject(f);
    }
    return total;
}

// Tallest element in the same run, used to size the off-screen buffer for
// upside-down lines.
static int MeasureTextLineHeight(const std::vector<PrinterElement>& elements, size_t startIdx, int fontSize) {
    int maxHeight = 0;
    for (size_t i = startIdx; i < elements.size(); ++i) {
        if (elements[i].type != ELEMENT_TEXT) break;
        int h = ElementFontHeight(elements[i], fontSize);
        if (h > maxHeight) maxHeight = h;
    }
    return maxHeight;
}

static bool LineHasUpsideDown(const std::vector<PrinterElement>& elements, size_t startIdx) {
    for (size_t i = startIdx; i < elements.size(); ++i) {
        if (elements[i].type != ELEMENT_TEXT) break;
        if (elements[i].isUpsideDown) return true;
    }
    return false;
}

// Draws one text segment at (x, y) and reports the space it took up. Used both
// for normal output and when rendering a line into an off-screen buffer.
static void DrawTextSegment(HDC hdc, const PrinterElement& el, int x, int y,
                            int fontSize, int charWidth, HFONT hFontNormal, SIZE* outSize) {
    bool deleteFont = false;
    HFONT font = SelectElementFont(el, fontSize, charWidth, hFontNormal, &deleteFont);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextCharacterExtra(hdc, ElementCharExtra(el, charWidth));

    SIZE extent;
    GetTextExtentPoint32(hdc, el.text.c_str(), (int)el.text.length(), &extent);
    if (el.isRotated90) {
        // A rotated run occupies one square cell per glyph.
        extent.cx = RotatedCellWidth(el, fontSize, charWidth) * (int)el.text.length();
        extent.cy = ElementFontHeight(el, fontSize);
    }

    COLORREF ink = el.isRed ? RGB(255, 0, 0) : RGB(0, 0, 0);
    if (el.isReverse) {
        // GS B: the ink colour fills the character cells and the glyphs are
        // knocked out in white.
        RECT cell = { x, y, x + extent.cx, y + extent.cy };
        HBRUSH brush = CreateSolidBrush(ink);
        FillRect(hdc, &cell, brush);
        DeleteObject(brush);
        SetTextColor(hdc, RGB(255, 255, 255));
    } else {
        SetTextColor(hdc, ink);
    }

    if (el.isRotated90) {
        // ESC V: each glyph is turned 90 degrees clockwise but the line still
        // runs left to right, so each one gets its own transform and cell.
        // The matrix maps logical +x to device +y and logical +y to device -x.
        int cell = RotatedCellWidth(el, fontSize, charWidth);
        int glyphHeight = ElementFontHeight(el, fontSize);
        SetTextCharacterExtra(hdc, 0);
        XFORM saved;
        GetWorldTransform(hdc, &saved);
        for (size_t i = 0; i < el.text.length(); ++i) {
            XFORM xf;
            xf.eM11 = 0.0f;  xf.eM12 = 1.0f;
            xf.eM21 = -1.0f; xf.eM22 = 0.0f;
            xf.eDx = (FLOAT)(x + (int)i * cell + glyphHeight);
            xf.eDy = (FLOAT)y;
            SetWorldTransform(hdc, &xf);
            TextOut(hdc, 0, 0, &el.text[i], 1);
        }
        SetWorldTransform(hdc, &saved);
    } else {
        TextOut(hdc, x, y, el.text.c_str(), (int)el.text.length());
    }

    SetTextCharacterExtra(hdc, 0);
    SelectObject(hdc, oldFont);
    if (deleteFont) DeleteObject(font);

    if (outSize) {
        outSize->cx = extent.cx;
        outSize->cy = ElementFontHeight(el, fontSize);
    }
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
            if (g_rawBuffer.empty()) return 0;
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
        case IDM_LIMPAR:
        {
             if (g_rawBuffer.empty()) return 0;
             if (MessageBox(hwnd, L"Tem a certeza que deseja limpar tudo?", L"Confirmar", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                 printer.Clear();
                 g_rawBuffer.clear();
                 // Force repaint
                 InvalidateRect(hwnd, NULL, TRUE);
             }
             return 0;
        }
        case IDM_SAIR:
            DestroyWindow(hwnd);
            return 0;
        case IDM_FONTE:
        {
            INT_PTR newSize = ShowInputDialog(hwnd, L"Tamanho do texto", g_fontSize);
            if (newSize > 0) {
                g_fontSize = (int)newSize;
                SaveSettings();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        case IDM_ALWAYSONTOP:
        {
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(hwnd, g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            
            // Update menu checkmark
            HMENU hMenu = GetMenu(hwnd);
            if (hMenu) {
                CheckMenuItem(hMenu, IDM_ALWAYSONTOP, g_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED);
            }
            
            SaveSettings();
            return 0;
        }
        case IDM_INSTALAR_DRIVER:
        {
            wchar_t psPath[MAX_PATH];
            if (SearchPath(NULL, L"powershell.exe", NULL, MAX_PATH, psPath, NULL) == 0) {
                MessageBox(hwnd, L"PowerShell indisponivel neste sistema. A instalação automática do driver não pode prosseguir.", STR_INSTALAR_IMPRESSORA, MB_OK | MB_ICONERROR);
                return 0;
            }

            const wchar_t* psCmd =
                L"-NoProfile -ExecutionPolicy Bypass -Command \""
                L"try {"
                L"  $alreadyExists = [bool](Get-Printer -Name 'VirtualESCPOS' -ErrorAction SilentlyContinue);"
                L"  if (-not (Get-PrinterDriver -Name 'Generic / Text Only' -ErrorAction SilentlyContinue)) {"
                L"    Add-PrinterDriver -Name 'Generic / Text Only' -ErrorAction Stop"
                L"  };"
                L"  if (-not (Get-PrinterPort -Name 'VirtualESCPOS_Port' -ErrorAction SilentlyContinue)) {"
                L"    Add-PrinterPort -Name 'VirtualESCPOS_Port' -PrinterHostAddress '127.0.0.1' -PortNumber 9100 -ErrorAction Stop"
                L"  };"
                L"  if (-not $alreadyExists) {"
                L"    Add-Printer -Name 'VirtualESCPOS' -DriverName 'Generic / Text Only' -PortName 'VirtualESCPOS_Port' -ErrorAction Stop;"
                L"    exit 0"
                L"  } else {"
                L"    exit 2"
                L"  }"
                L"} catch { exit 1 }\"";

            SHELLEXECUTEINFO sei = { 0 };
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.hwnd = hwnd;
            sei.lpVerb = L"runas";
            sei.lpFile = L"powershell.exe";
            sei.lpParameters = psCmd;
            sei.nShow = SW_HIDE;

            if (!ShellExecuteEx(&sei)) {
                DWORD err = GetLastError();
                if (err == ERROR_CANCELLED) {
                    MessageBox(hwnd, L"Cancelado pelo utilizador.", STR_INSTALAR_IMPRESSORA, MB_OK | MB_ICONWARNING);
                } else if (err == ERROR_FILE_NOT_FOUND) {
                    MessageBox(hwnd, L"PowerShell indisponivel neste sistema.", STR_INSTALAR_IMPRESSORA, MB_OK | MB_ICONERROR);
                } else {
                    MessageBox(hwnd, L"Impossivel iniciar a instalação da impressora.", STR_INSTALAR_IMPRESSORA, MB_OK | MB_ICONERROR);
                }
                return 0;
            }

            WaitForSingleObject(sei.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);

            if (exitCode == 0) {
                MessageBox(hwnd, L"Impressora 'VirtualESCPOS' instalada com sucesso.", STR_INSTALAR_IMPRESSORA, MB_OK | MB_ICONINFORMATION);
            } else if (exitCode == 2) {
                MessageBox(hwnd, L"A impressora 'VirtualESCPOS' já se encontra instalada.", STR_INSTALAR_IMPRESSORA, MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBox(hwnd, L"Falha ao instalar a impressora. Verifique se tem privilegios de administrador e se o driver 'Generic / Text Only' está disponível.", STR_INSTALAR_IMPRESSORA, MB_OK | MB_ICONERROR);
            }
            return 0;
        }
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
        int currentLineMaxHeight = g_fontSize + 4; // Default line height

        // Create font
        HFONT hFontNormal = CreateFont(g_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
        
        HGDIOBJ oldFont = SelectObject(hdc, hFontNormal);
        SetBkMode(hdc, TRANSPARENT);
        
        // Create Pen for cut lines
        HPEN hPenCut = CreatePen(PS_DASH, 1, RGB(100, 100, 100));
        HGDIOBJ oldPen = SelectObject(hdc, hPenCut);

        // Reference width for justification (ESC a). When the user has set a
        // fixed column count ("Colunas"), center/right within that paper width
        // (columns * character width); otherwise fall back to the window width.
        TEXTMETRIC tm;
        GetTextMetrics(hdc, &tm); // hFontNormal is selected here
        int charWidth = tm.tmAveCharWidth;
        int paperWidth = (g_colunas > 0) ? g_colunas * charWidth : 0;

        // ESC V needs escapement and orientation to differ, which GDI only
        // allows in advanced graphics mode.
        SetGraphicsMode(hdc, GM_ADVANCED);

        // Left edge of the printable area for an element, honouring GS L.
        auto elementBaseX = [&](const PrinterElement& el) -> int {
            return leftMargin + DotsToPixels(el.marginLeft, charWidth);
        };

        // Width of the printable area: GS W if set, else the configured paper
        // width, else whatever the window gives us.
        auto elementAreaWidth = [&](const PrinterElement& el) -> int {
            if (el.areaWidth > 0) return DotsToPixels(el.areaWidth, charWidth);
            if (paperWidth > 0) return paperWidth;
            return rect.right - elementBaseX(el) - leftMargin;
        };

        auto alignStartX = [&](int contentWidth, const PrinterElement& el) -> int {
            int base = elementBaseX(el);
            int areaW = elementAreaWidth(el);
            int startX = base;
            if (el.align == 1) startX = base + (areaW - contentWidth) / 2;
            else if (el.align == 2) startX = base + areaW - contentWidth;
            if (startX < base) startX = base;
            return startX;
        };

        bool atLineStart = true;
        for (size_t idx = 0; idx < elements.size(); ++idx) {
            const PrinterElement& el = elements[idx];
            if (el.type == ELEMENT_TEXT) {
                // At the first text segment of a line, offset the start position
                // for center/right justification based on the whole line width.
                if (atLineStart) {
                    int lineWidth = MeasureTextLineWidth(hdc, elements, idx, g_fontSize, charWidth, hFontNormal);
                    currentX = alignStartX(lineWidth, el);
                    atLineStart = false;

                    // ESC {: the printer turns the whole line 180 degrees, so
                    // render it off-screen and blit it flipped on both axes.
                    // Rotating per line rather than per segment is what puts
                    // the segments back in the order a real printer produces.
                    int lineHeight = MeasureTextLineHeight(elements, idx, g_fontSize);
                    if (LineHasUpsideDown(elements, idx) && lineWidth > 0 && lineHeight > 0) {
                        HDC memDC = CreateCompatibleDC(hdc);
                        HBITMAP memBmp = CreateCompatibleBitmap(hdc, lineWidth, lineHeight);
                        HGDIOBJ oldMemBmp = SelectObject(memDC, memBmp);

                        RECT full = { 0, 0, lineWidth, lineHeight };
                        FillRect(memDC, &full, (HBRUSH)GetStockObject(WHITE_BRUSH));
                        SetBkMode(memDC, TRANSPARENT);

                        int offscreenX = 0;
                        size_t j = idx;
                        for (; j < elements.size() && elements[j].type == ELEMENT_TEXT; ++j) {
                            SIZE segSize;
                            DrawTextSegment(memDC, elements[j], offscreenX, 0,
                                            g_fontSize, charWidth, hFontNormal, &segSize);
                            offscreenX += segSize.cx;
                        }

                        StretchBlt(hdc, currentX + lineWidth, y + lineHeight,
                                   -lineWidth, -lineHeight,
                                   memDC, 0, 0, lineWidth, lineHeight, SRCCOPY);

                        SelectObject(memDC, oldMemBmp);
                        DeleteObject(memBmp);
                        DeleteDC(memDC);

                        if (lineHeight > currentLineMaxHeight) currentLineMaxHeight = lineHeight;
                        currentX += lineWidth;
                        idx = j - 1; // the loop's ++idx steps past the whole run
                        continue;
                    }
                }

                SIZE segSize;
                DrawTextSegment(hdc, el, currentX, y, g_fontSize, charWidth, hFontNormal, &segSize);

                if (segSize.cy > currentLineMaxHeight) {
                    currentLineMaxHeight = segSize.cy;
                }
                currentX += segSize.cx;
            }
            else if (el.type == ELEMENT_SETPOS) {
                // ESC $ / ESC \: an explicit position replaces the justified
                // start, so the line is no longer "at its start".
                int base = elementBaseX(el);
                currentX = el.absolutePos ? base + DotsToPixels(el.width, charWidth)
                                          : currentX + DotsToPixels(el.width, charWidth);
                if (currentX < base) currentX = base;
                atLineStart = false;
            }
            else if (el.type == ELEMENT_FEED) {
                // ESC J / ESC K: vertical dots map 1:1 to pixels, matching how
                // ESC 3 line spacing is already handled.
                y += el.height;
                currentX = elementBaseX(el);
                atLineStart = true;
                currentLineMaxHeight = g_fontSize + 4;
            }
            else if (el.type == ELEMENT_NEWLINE) {
                currentX = leftMargin;
                atLineStart = true;
                // Add vertical spacing
                // Use explicit spacing if set (el.height).
                if (el.height > 0) {
                     y += el.height;
                } else {
                     y += currentLineMaxHeight + 4; // Use +4 padding as a safe baseline
                }
                currentLineMaxHeight = g_fontSize + 4; // Reset to default min
            }
            else if (el.type == ELEMENT_CUT) {
                // Should force newline first just in case?
                if (currentX != leftMargin) {
                    currentX = leftMargin; // Actually just newline
                    y += currentLineMaxHeight + 4;
                    currentLineMaxHeight = g_fontSize + 4;
                }
                
                y += 10;
                MoveToEx(hdc, 0, y, NULL);
                LineTo(hdc, rect.right, y);
                // Draw scissors or text "CUT"
                SetTextColor(hdc, RGB(0, 0, 0));
                SelectObject(hdc, hFontNormal);
                TextOut(hdc, rect.right - 50, y - 8, L"[CUT]", 5);
                y += 10;
                y += g_fontSize + 4; // Advance paper a bit after cut (default line height)
                atLineStart = true;
            }
            else if (el.type == ELEMENT_BITMAP) {
                if (currentX != leftMargin) {
                    currentX = leftMargin;
                    y += currentLineMaxHeight + 4;
                    currentLineMaxHeight = g_fontSize + 4;
                }
                atLineStart = true;

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

                // Apply justification (ESC a): center/right within the paper width
                int drawX = alignStartX(w, el);

                SetDIBitsToDevice(hdc, drawX, y, w, h,
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
    
    if (g_alwaysOnTop) {
        SetWindowPos(hMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    
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
