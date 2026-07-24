#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <iostream>
#include <mutex>
#include <string>
#include <vector>

// Represents a drawable element on the simulated paper
enum ElementType {
  ELEMENT_TEXT,
  ELEMENT_NEWLINE, // Explicit line break
  ELEMENT_BITMAP,
  ELEMENT_CUT
};

struct PrinterElement {
  ElementType type;
  std::wstring text; // For text elements
  bool isRed;        // For 1B 45 1 (Red) vs 0 (Black)
  bool isDoubleWidth;
  bool isDoubleHeight;
  bool isUnderline;                      // For 1B 2D n
  std::vector<unsigned char> bitmapData; // For bitmap elements
  bool isColumnFormat; // True = Column-major (GS *), False = Row-major/Raster
                       // (GS v 0)
  bool mergeableBand = false; // True for ESC * graphics bands: consecutive
                              // bands separated by a single line feed are
                              // stacked into one contiguous bitmap.
  int align = 0; // Justification: 0 = left, 1 = center, 2 = right (ESC a n)
  int width;
  int height;
  // x is determined at render time for flow
};

class VirtualPrinter {
public:
  VirtualPrinter();
  ~VirtualPrinter();

  void Reset();
  void Clear();
  void ProcessData(const unsigned char *data, int length);
  std::vector<PrinterElement> GetElements();
  void SetRepaintCallback(void (*callback)(void *), void *param);
  void SetMaxColumns(int cols);

private:
  std::vector<PrinterElement> elements;
  std::mutex mutex;
  void (*repaintCallback)(void *);
  void *repaintParam;

  // Parser parsing state
  enum ParseState {
    STATE_NORMAL,
    STATE_ESC,
    STATE_ESC_EXCLAMATION, // ESC ! n
    STATE_ESC_E,           // ESC E n
    STATE_ESC_MINUS,       // ESC - n
    STATE_ESC_d,           // ESC d n
    STATE_ESC_3,           // ESC 3 n (Set line spacing)
    STATE_ESC_a,           // ESC a n (Select justification)

    // ESC * m nL nH d... (Select bit image mode - legacy column graphics)
    STATE_ESC_STAR,      // ESC * m
    STATE_ESC_STAR_nL,   // ESC * m nL
    STATE_ESC_STAR_nH,   // ESC * m nL nH
    STATE_ESC_STAR_DATA, // ESC * ... image data

    STATE_GS,
    STATE_GS_V,
    STATE_GS_V_n,
    STATE_GS_v,
    STATE_GS_v_0,
    STATE_GS_v_0_xL,
    STATE_GS_v_0_xH,
    STATE_GS_v_0_yL,
    STATE_GS_v_0_yH,
    STATE_GS_v_0_DATA,

    // New states for ignoring/handling additional commands
    STATE_ESC_c,
    STATE_ESC_c_4, // ESC c 4 n
    STATE_ESC_c_5, // ESC c 5 n
    STATE_ESC_t,   // ESC t n

    // New states for Download Bit Image (GS * x y d...)
    STATE_GS_STAR,
    STATE_GS_STAR_y,
    STATE_GS_STAR_DATA,

    // New states for Print Download Bit Image (GS / m)
    STATE_GS_SLASH
  };

  ParseState state;

  // Formatting state
  bool isRedMode; // True = Red (Emphasized), False = Black
  bool isDoubleWidthMode;
  bool isDoubleHeightMode;
  bool isUnderlineMode;

  // Line Spacing
  int currentLineSpacing; // -1 = Default (Auto based on font), >=0 = Fixed
                          // value

  // Downloaded Bitmap (GS *)
  std::vector<unsigned char> downloadedBitmap;
  int downloadedBitmapWidthBytes;  // x
  int downloadedBitmapHeightBytes; // y
  int downloadedBitmapExpected;

  // Code Page
  int currentCodePage; // 0=PC437, 2=PC850, 3=PC860, etc.

  // Justification (ESC a n): 0 = left, 1 = center, 2 = right
  int currentAlign;

  // Max columns (0 = disabled)
  int maxColumns;
  int currentColumn; // Current column position for auto-CRLF

  // Current text buffer
  std::wstring currentText;

  // Bitmap processing variables
  int bitmapMode;
  int bitmapWidthBytes;
  int bitmapHeightDots;
  int bitmapDataExpected;
  std::vector<unsigned char> currentBitmapData;

  // ESC * (Select bit image mode) - legacy column-format graphics
  int escStarMode;           // m
  int escStarColumns;        // nL + nH*256 (horizontal dots)
  int escStarBytesPerColumn; // 1 (8-dot) or 3 (24-dot)
  int escStarBandHeight;     // bytesPerColumn * 8
  int escStarDataExpected;   // columns * bytesPerColumn
  std::vector<unsigned char> escStarData;

  void FlushSegment();
  void AddNewLine();
  void AddCutLine();
  void CommitEscStarBand();
};
