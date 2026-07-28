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
  ELEMENT_CUT,
  ELEMENT_SETPOS, // ESC $ / ESC \ - move the horizontal print position
  ELEMENT_FEED,   // ESC J / ESC K - feed the paper by a number of dots
  // Page mode (ESC L): the elements between BEGIN and END are positioned by
  // their own pageX/pageY instead of flowing, and BEGIN carries the print
  // area set by ESC W.
  ELEMENT_PAGE_BEGIN,
  ELEMENT_PAGE_END
};

// A character cell in Font A is 12 dots wide and 24 tall. Horizontal ESC/POS
// coordinates are expressed in dots, so this is the factor that maps them onto
// the character grid the renderer lays text out on.
const int DOTS_PER_CHAR = 12;

// Character font selected by ESC M n.
enum PrinterFont {
  FONT_A = 0, // 12x24 - the default, 48 columns on 80 mm paper
  FONT_B = 1, // 10x24 - the narrow one, 57 columns, on a cell as tall as A's
  FONT_C = 2  // smaller still on the models that offer it
};

struct PrinterElement {
  ElementType type;
  std::wstring text;  // For text elements
  bool isRed = false; // For 1B 45 1 (Red) vs 0 (Black)
  // Character size multipliers, 1..8 (ESC ! bits 4/5 and GS ! n).
  int widthScale = 1;
  int heightScale = 1;
  bool isReverse = false;    // GS B n - white on black
  bool isUpsideDown = false; // ESC { n - 180 degree rotation
  bool isBold = false;       // ESC G / ESC g n - double-strike
  bool isRotated90 = false;  // ESC V n - 90 degree clockwise rotation
  int charSpacing = 0;       // ESC SP n - extra dots after each character
  int marginLeft = 0;        // GS L - left margin, in dots
  int areaWidth = 0;         // GS W - print area width in dots (0 = full paper)
  bool absolutePos = false;  // ELEMENT_SETPOS: absolute (ESC $) vs relative
  int font = FONT_A;         // ESC M n
  bool isUnderline = false;              // For 1B 2D n
  std::vector<unsigned char> bitmapData; // For bitmap elements
  bool isColumnFormat; // True = Column-major (GS *), False = Row-major/Raster
                       // (GS v 0)
  bool mergeableBand = false; // True for ESC * graphics bands: consecutive
                              // bands separated by a single line feed are
                              // stacked into one contiguous bitmap.
  int align = 0; // Justification: 0 = left, 1 = center, 2 = right (ESC a n)
  // Meaning depends on the type: dots of movement for SETPOS/FEED, dot size
  // for BITMAP, print area size for PAGE_BEGIN/PAGE_END, line spacing for
  // NEWLINE.
  int width = 0;
  int height = 0;
  // x is determined at render time for flow
  // --- Page mode -----------------------------------------------------------
  // For elements inside a page: the position, in dots, within the print area,
  // expressed in the coordinate system of pageDir. For ELEMENT_PAGE_BEGIN:
  // the origin of the print area (ESC W x, y) and width/height carry its size.
  int pageX = 0;
  int pageY = 0;
  int pageDir = 0; // ESC T n: 0 left-right, 1 bottom-top, 2 right-left,
                   //          3 top-bottom
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
    STATE_ESC_BRACE,       // ESC { n (upside-down)
    STATE_ESC_M,           // ESC M n (character font)
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
    STATE_ESC_c, // ESC c 0/1/3/4/5 n
    STATE_ESC_t, // ESC t n

    // New states for Download Bit Image (GS * x y d...)
    STATE_GS_STAR,
    STATE_GS_STAR_y,
    STATE_GS_STAR_DATA,

    // New states for Print Download Bit Image (GS / m)
    STATE_GS_SLASH,

    // --- Generic parameter consumption -------------------------------------
    // Commands we recognise but do not render still have to swallow their
    // parameters; otherwise those bytes fall through to the text stream and
    // are printed as garbage.
    STATE_SKIP_N,   // consume `skipRemaining` bytes, then STATE_NORMAL
    STATE_READ_LEN, // read a little-endian length, then skip that many

    STATE_PAREN_fn, // ESC ( fn pL pH ... (consumed only)
    STATE_GS_8,     // GS 8 L p1 p2 p3 p4 ...

    // GS ( <id> pL pH d1...dk. The identifier selects a command group: 'k' is
    // the 2D code group (QR Code), 'L' the raster graphics group.
    STATE_GS_PAREN_id,
    STATE_GS_PAREN_pL,
    STATE_GS_PAREN_pH,
    STATE_GS_PAREN_DATA,

    // ESC & y c1 c2 [x d1...d(x*y)]... (define user-defined characters)
    STATE_ESC_AMP_y,
    STATE_ESC_AMP_c1,
    STATE_ESC_AMP_c2,
    STATE_ESC_AMP_x,

    STATE_ESC_D, // ESC D n1...nk NUL (set horizontal tab positions)

    // DLE (real-time commands)
    STATE_DLE,
    STATE_DLE_DC4,

    // FS (two-byte command set)
    STATE_FS,
    STATE_FS_q_n,    // FS q n
    STATE_FS_q_HDR,  // FS q ... xL xH yL yH
    STATE_FS_q_DATA, // FS q ... image data
    STATE_FS_p_n,    // FS p n
    STATE_FS_p_m,    // FS p n m

    STATE_GS_8_LEN, // GS 8 L p1 p2 p3 p4 (32-bit payload length)

    // Barcodes: GS k m d1...dk NUL (function A)
    //           GS k m n d1...dn  (function B)
    STATE_GS_k,
    STATE_GS_k_DATA_A, // function A: collect until NUL
    STATE_GS_k_n,      // function B: read n
    STATE_GS_k_DATA_B, // function B: collect n bytes

    // Barcode settings that take a single parameter
    STATE_GS_h, // GS h n - barcode height
    STATE_GS_w, // GS w n - barcode module width
    STATE_GS_H, // GS H n - HRI position
    STATE_GS_f, // GS f n - HRI font

    STATE_GS_EXCLAMATION, // GS ! n - character size
    STATE_GS_B,           // GS B n - reverse printing

    // Text attributes
    STATE_ESC_G,     // ESC G n / ESC g n - double-strike
    STATE_ESC_r,     // ESC r n - select print colour
    STATE_ESC_SP,    // ESC SP n - right-side character spacing
    STATE_ESC_V,     // ESC V n - 90 degree rotation

    // Horizontal positioning: ESC $ nL nH and ESC \ nL nH
    STATE_ESC_DOLLAR_nL,
    STATE_ESC_DOLLAR_nH,
    STATE_ESC_BSLASH_nL,
    STATE_ESC_BSLASH_nH,

    // GS L nL nH (left margin) and GS W nL nH (print area width)
    STATE_GS_L_nL,
    STATE_GS_L_nH,
    STATE_GS_W_nL,
    STATE_GS_W_nH,

    // Feeds: ESC J n / ESC K n (dots) and ESC e n (reverse lines)
    STATE_ESC_J,
    STATE_ESC_K,
    STATE_ESC_e,

    // Page mode: ESC T n (print direction) and
    // ESC W xL xH yL yH dxL dxH dyL dyH (print area)
    STATE_ESC_T,
    STATE_ESC_W,

    // GS $ nL nH / GS \ nL nH - vertical print position in page mode
    STATE_GS_DOLLAR_nL,
    STATE_GS_DOLLAR_nH,
    STATE_GS_BSLASH_nL,
    STATE_GS_BSLASH_nH
  };

  ParseState state;

  // Formatting state
  // ESC E (emphasized) and ESC r (print colour) are separate registers on a
  // real printer but both come out red here, so they are tracked separately
  // and combined when an element is emitted.
  bool isEmphasizedMode; // ESC E n / ESC ! bit 3
  bool isColorRedMode;   // ESC r n
  int widthScaleMode;    // 1..8
  int heightScaleMode;   // 1..8
  bool isReverseMode;
  bool isUpsideDownMode;
  bool isBoldMode;      // ESC G / ESC g
  bool isRotated90Mode; // ESC V
  int charSpacingDots;  // ESC SP n
  int marginLeftDots;   // GS L
  int areaWidthDots;    // GS W
  int currentFont;
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

  // --- Page mode (ESC L) ----------------------------------------------------
  // While page mode is active nothing reaches the paper: elements are buffered
  // with an explicit position inside the print area and only committed when
  // FF / ESC FF prints the page.
  bool pageMode;
  int pageOriginX;   // ESC W x, in dots from the left of the printable area
  int pageOriginY;   // ESC W y, in dots down the paper
  int pageAreaW;     // ESC W dx, 0 = never set (renderer sizes to content)
  int pageAreaH;     // ESC W dy
  int pageDirection; // ESC T n, 0..3
  int pageCursorX;   // print position along the text flow, in dots
  int pageCursorY;   // print position across lines, in dots
  std::vector<PrinterElement> pageElements;
  std::vector<unsigned char> pendingParams; // ESC W's eight parameter bytes

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

  // --- Parameter consumption bookkeeping ------------------------------------
  long long skipRemaining;    // STATE_SKIP_N
  ParseState skipReturnState; // state to enter once the skip completes
  int pendingParam;           // low byte of a 16-bit nL/nH parameter pair
  int lenBytesRemaining;      // STATE_READ_LEN: length bytes still to read
  int lenShift;            // STATE_READ_LEN: current byte's shift
  long long pendingLen;    // STATE_READ_LEN: accumulated length

  int userCharY;         // ESC &: bytes per column
  int userCharRemaining; // ESC &: characters still to read

  int nvImagesRemaining;     // FS q: images still to read
  unsigned char nvHeader[4]; // FS q: xL xH yL yH
  int nvHeaderIndex;
  long long nvExpected;      // FS q: bytes left in the current image
  std::vector<unsigned char> nvBuffer;
  int nvImageIndex; // FS p: which stored image to print

  // A bitmap held in the printer, either an NV image (FS q) or the raster
  // graphics buffer (GS ( L / GS 8 L).
  struct StoredImage {
    int widthDots = 0;
    int heightDots = 0;
    int scaleX = 1; // GS ( L bx, applied when the image is printed
    int scaleY = 1; // GS ( L by
    std::vector<unsigned char> raster; // row-major, MSB = leftmost dot
  };
  std::vector<StoredImage> nvImages; // FS q, addressed from 1 by FS p
  StoredImage graphicsBuffer;        // GS ( L fn 112, printed by fn 50

  // Horizontal tab positions (ESC D), in columns. Empty = default every 8.
  std::vector<int> tabStops;

  // --- Barcodes (GS k and its settings) -------------------------------------
  int barcodeType;      // BarcodeType resolved from the GS k `m` parameter
  int barcodeHeight;    // GS h n, in dots
  int barcodeModule;    // GS w n, narrow element width in dots
  int barcodeHriPos;    // GS H n: 0 none, 1 above, 2 below, 3 both
  int barcodeHriFont;   // GS f n
  int barcodeExpected;  // function B: bytes still to collect
  std::vector<unsigned char> barcodeData;

  // --- GS ( <id> command groups ---------------------------------------------
  int parenId;       // the identifier byte ('k', 'L', ...)
  long long parenExpected; // payload length from pL/pH (or GS 8 L's 32 bits)
  std::vector<unsigned char> parenData;

  // QR Code state (GS ( k, cn = 49)
  int qrModuleSize;  // fn 67: dots per module
  int qrEcLevel;     // fn 69: 0 = L, 1 = M, 2 = Q, 3 = H
  std::vector<unsigned char> qrStoredData; // fn 80: symbol storage area

  void FlushSegment();
  void AddNewLine();
  void AddCutLine();
  void CommitEscStarBand();

  // --- Page mode helpers ----------------------------------------------------
  // The buffer new elements go to: the page buffer while page mode is active.
  std::vector<PrinterElement> &Target();
  // Appends an element, stamping it with the page position in page mode and
  // advancing the print position past it.
  void PushElement(PrinterElement &el);
  // Enters page mode (ESC L) and clears the page buffer.
  void EnterPageMode();
  // Leaves page mode. `print` commits the buffered page to the paper
  // (FF, ESC FF); otherwise the page is discarded (CAN, ESC S, ESC @).
  void LeavePageMode(bool print, bool stayInPageMode = false);
  // Width/height of one character cell in dots, with the current font and
  // size multipliers applied. Used to advance the page-mode print position.
  int CharWidthDots() const;
  int CharHeightDots() const;
  // Length of the print area along the current text flow direction, in dots
  // (0 when ESC W never ran).
  int PageFlowLength() const;

  // Consume `n` bytes of parameters, then enter `next`.
  void SkipBytes(long long n, ParseState next = STATE_NORMAL);
  // Read a little-endian length of `numLenBytes` bytes, then skip that many.
  void BeginLengthSkip(int numLenBytes);
  // HT (0x09): advance to the next tab stop by padding with spaces.
  void HandleTab();
  // Encodes the collected GS k data and appends it to the paper.
  void CommitBarcode();
  // Emits a horizontal move (ESC $ / ESC \).
  void AddSetPos(int dots, bool absolute);
  // Emits a vertical feed in dots (ESC J / ESC K); negative feeds backwards.
  void AddFeed(int dots);
  // Copies the current formatting state onto an element being emitted.
  void ApplyStyle(PrinterElement &el);
  // Dispatches a completed GS ( <id> payload.
  void HandleParenCommand();
  // Handles the 2D code group (GS ( k): QR Code settings, store and print.
  void Handle2DCodeCommand();
  // Handles the raster graphics group (GS ( L / GS 8 L).
  void HandleGraphicsCommand();
  // Stores the FS q image currently in nvBuffer, converting it to raster.
  void StoreNvImage();
  // Emits a stored image, scaled by the mode byte of FS p / GS ( L.
  void PrintStoredImage(const StoredImage &img, int widthScale, int heightScale);
  // Encodes the stored QR data and appends the symbol to the paper.
  void CommitQRCode();
  // Appends a raster bitmap element built from packed 1bpp rows.
  void AddBitmapElement(const std::vector<unsigned char> &raster, int widthDots,
                        int heightDots);
};
