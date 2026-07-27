#include "Barcode.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Low-level emit helpers
//
// Every encoder appends dots directly at printer resolution: `narrow` is the
// module width from GS w, and two-width symbologies (CODE39, ITF, CODABAR) use
// `WideWidth` for their wide elements. The 8/3 factor reproduces the element
// widths Epson printers use (n=2 -> 5 dots, n=3 -> 8, n=4 -> 11, ...), which
// lands inside the 2:1..3:1 ratio those symbologies require.
// ---------------------------------------------------------------------------

static int WideWidth(int narrow) { return (narrow * 8 + 1) / 3; }

static void Run(std::vector<bool> &out, bool value, int count) {
    for (int i = 0; i < count; ++i) out.push_back(value);
}

// Appends a binary module pattern such as "0001101" (1 = bar).
static void AppendBits(std::vector<bool> &out, const char *bits, int narrow) {
    for (const char *p = bits; *p; ++p) Run(out, *p == '1', narrow);
}

// Appends elements that alternate bar/space, taking their widths from a string
// of digits such as "212222" (CODE128).
static void AppendWidths(std::vector<bool> &out, const char *widths, int narrow) {
    bool bar = true;
    for (const char *p = widths; *p; ++p) {
        Run(out, bar, (*p - '0') * narrow);
        bar = !bar;
    }
}

// Appends elements that alternate bar/space, taking their widths from a string
// of 'n'/'w' flags such as "nnnwwnwnn" (CODE39, ITF, CODABAR).
static void AppendNW(std::vector<bool> &out, const char *pattern, int narrow) {
    int wide = WideWidth(narrow);
    bool bar = true;
    for (const char *p = pattern; *p; ++p) {
        Run(out, bar, (*p == 'w') ? wide : narrow);
        bar = !bar;
    }
}

static bool IsDigits(const std::string &s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// UPC / EAN
// ---------------------------------------------------------------------------

static const char *EAN_L[10] = {"0001101", "0011001", "0010011", "0111101",
                                "0100011", "0110001", "0101111", "0111011",
                                "0110111", "0001011"};
static const char *EAN_G[10] = {"0100111", "0110011", "0011011", "0100001",
                                "0011101", "0111001", "0000101", "0010001",
                                "0001001", "0010111"};
static const char *EAN_R[10] = {"1110010", "1100110", "1101100", "1000010",
                                "1011100", "1001110", "1010000", "1000100",
                                "1001000", "1110100"};

// Parity of digits 2..7 of an EAN-13, selected by the first digit.
static const char *EAN13_PARITY[10] = {"LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL",
                                       "LGLLGG", "LGGLLG", "LGGGLL", "LGLGLG",
                                       "LGLGGL", "LGGLGL"};

// Parity of the six UPC-E digits, selected by the check digit (number system 0).
static const char *UPCE_PARITY[10] = {"EEEOOO", "EEOEOO", "EEOOEO", "EEOOOE",
                                      "EOEEOO", "EOOEEO", "EOOOEE", "EOEOEO",
                                      "EOEOOE", "EOOEOE"};

// Modulo-10 check digit over `digits` (which excludes the check digit itself).
static int EanCheckDigit(const std::string &digits) {
    int sum = 0;
    int weight = 3;
    for (int i = (int)digits.size() - 1; i >= 0; --i) {
        sum += (digits[i] - '0') * weight;
        weight = (weight == 3) ? 1 : 3;
    }
    return (10 - (sum % 10)) % 10;
}

static bool EncodeEan13(const std::string &in, int narrow,
                        std::vector<bool> &out, std::string &hri) {
    if (!IsDigits(in) || (in.size() != 12 && in.size() != 13)) return false;
    std::string d = in.substr(0, 12);
    d += (char)('0' + (in.size() == 13 ? in[12] - '0' : EanCheckDigit(d)));

    const char *parity = EAN13_PARITY[d[0] - '0'];
    AppendBits(out, "101", narrow);
    for (int i = 1; i <= 6; ++i) {
        int v = d[i] - '0';
        AppendBits(out, parity[i - 1] == 'L' ? EAN_L[v] : EAN_G[v], narrow);
    }
    AppendBits(out, "01010", narrow);
    for (int i = 7; i <= 12; ++i) AppendBits(out, EAN_R[d[i] - '0'], narrow);
    AppendBits(out, "101", narrow);
    hri = d;
    return true;
}

static bool EncodeEan8(const std::string &in, int narrow,
                       std::vector<bool> &out, std::string &hri) {
    if (!IsDigits(in) || (in.size() != 7 && in.size() != 8)) return false;
    std::string d = in.substr(0, 7);
    d += (char)('0' + (in.size() == 8 ? in[7] - '0' : EanCheckDigit(d)));

    AppendBits(out, "101", narrow);
    for (int i = 0; i < 4; ++i) AppendBits(out, EAN_L[d[i] - '0'], narrow);
    AppendBits(out, "01010", narrow);
    for (int i = 4; i < 8; ++i) AppendBits(out, EAN_R[d[i] - '0'], narrow);
    AppendBits(out, "101", narrow);
    hri = d;
    return true;
}

static bool EncodeUpcA(const std::string &in, int narrow,
                       std::vector<bool> &out, std::string &hri) {
    if (!IsDigits(in) || (in.size() != 11 && in.size() != 12)) return false;
    std::string d = in.substr(0, 11);
    d += (char)('0' + (in.size() == 12 ? in[11] - '0' : EanCheckDigit(d)));

    AppendBits(out, "101", narrow);
    for (int i = 0; i < 6; ++i) AppendBits(out, EAN_L[d[i] - '0'], narrow);
    AppendBits(out, "01010", narrow);
    for (int i = 6; i < 12; ++i) AppendBits(out, EAN_R[d[i] - '0'], narrow);
    AppendBits(out, "101", narrow);
    hri = d;
    return true;
}

// Expands the six compressed UPC-E digits into the 11-digit UPC-A body so the
// check digit (which drives the parity pattern) can be derived.
static bool UpcExpand(char numberSystem, const std::string &six,
                      std::string &upcaBody) {
    if (six.size() != 6) return false;
    char d1 = six[0], d2 = six[1], d3 = six[2];
    char d4 = six[3], d5 = six[4], d6 = six[5];
    upcaBody = std::string(1, numberSystem);
    switch (d6) {
    case '0':
    case '1':
    case '2':
        upcaBody += d1; upcaBody += d2; upcaBody += d6;
        upcaBody += "0000";
        upcaBody += d3; upcaBody += d4; upcaBody += d5;
        break;
    case '3':
        upcaBody += d1; upcaBody += d2; upcaBody += d3;
        upcaBody += "00000";
        upcaBody += d4; upcaBody += d5;
        break;
    case '4':
        upcaBody += d1; upcaBody += d2; upcaBody += d3; upcaBody += d4;
        upcaBody += "00000";
        upcaBody += d5;
        break;
    default: // 5..9
        upcaBody += d1; upcaBody += d2; upcaBody += d3; upcaBody += d4;
        upcaBody += d5;
        upcaBody += "0000";
        upcaBody += d6;
        break;
    }
    return upcaBody.size() == 11;
}

static bool EncodeUpcE(const std::string &in, int narrow,
                       std::vector<bool> &out, std::string &hri) {
    if (!IsDigits(in)) return false;

    char numberSystem = '0';
    std::string six;
    if (in.size() == 6) {
        six = in;
    } else if (in.size() == 7 || in.size() == 8) {
        if (in[0] != '0' && in[0] != '1') return false;
        numberSystem = in[0];
        six = in.substr(1, 6);
    } else {
        return false;
    }

    std::string body;
    if (!UpcExpand(numberSystem, six, body)) return false;
    int check = EanCheckDigit(body);

    const char *parity = UPCE_PARITY[check];
    AppendBits(out, "101", narrow);
    for (int i = 0; i < 6; ++i) {
        int v = six[i] - '0';
        // Number system 1 uses the complement of the number system 0 pattern.
        bool even = (parity[i] == 'E');
        if (numberSystem == '1') even = !even;
        AppendBits(out, even ? EAN_G[v] : EAN_L[v], narrow);
    }
    AppendBits(out, "010101", narrow);

    hri = std::string(1, numberSystem) + six + (char)('0' + check);
    return true;
}

// ---------------------------------------------------------------------------
// CODE39
// ---------------------------------------------------------------------------

static const char CODE39_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%*";
static const char *CODE39_PATTERNS[] = {
    "nnnwwnwnn", "wnnwnnnnw", "nnwwnnnnw", "wnwwnnnnn", "nnnwwnnnw", // 0-4
    "wnnwwnnnn", "nnwwwnnnn", "nnnwnnwnw", "wnnwnnwnn", "nnwwnnwnn", // 5-9
    "wnnnnwnnw", "nnwnnwnnw", "wnwnnwnnn", "nnnnwwnnw", "wnnnwwnnn", // A-E
    "nnwnwwnnn", "nnnnnwwnw", "wnnnnwwnn", "nnwnnwwnn", "nnnnwwwnn", // F-J
    "wnnnnnnww", "nnwnnnnww", "wnwnnnnwn", "nnnnwnnww", "wnnnwnnwn", // K-O
    "nnwnwnnwn", "nnnnnnwww", "wnnnnnwwn", "nnwnnnwwn", "nnnnwnwwn", // P-T
    "wwnnnnnnw", "nwwnnnnnw", "wwwnnnnnn", "nwnnwnnnw", "wwnnwnnnn", // U-Y
    "nwwnwnnnn",                                                     // Z
    "nwnnnnwnw", "wwnnnnwnn", "nwwnnnwnn", "nwnwnwnnn", "nwnwnnnwn", // - . SP $ /
    "nwnnnwnwn", "nnnwnwnwn", "nwnnwnwnn"                            // + % *
};

static bool EncodeCode39(const std::string &in, int narrow,
                         std::vector<bool> &out, std::string &hri) {
    // Applications may or may not include the start/stop character; we always
    // emit it ourselves, so strip a supplied pair first.
    std::string d = in;
    if (d.size() >= 2 && d[0] == '*' && d[d.size() - 1] == '*') {
        d = d.substr(1, d.size() - 2);
    }
    if (d.empty()) return false;

    std::string full = "*" + d + "*";
    for (size_t i = 0; i < full.size(); ++i) {
        const char *pos = strchr(CODE39_CHARS, full[i]);
        if (!pos || full[i] == '\0') return false;
        if (i > 0) Run(out, false, narrow); // inter-character gap
        AppendNW(out, CODE39_PATTERNS[pos - CODE39_CHARS], narrow);
    }
    hri = d;
    return true;
}

// ---------------------------------------------------------------------------
// ITF (Interleaved 2 of 5)
// ---------------------------------------------------------------------------

static const char *ITF_PATTERNS[10] = {"nnwwn", "wnnnw", "nwnnw", "wwnnn",
                                       "nnwnw", "wnwnn", "nwwnn", "nnnww",
                                       "wnnwn", "nwnwn"};

static bool EncodeItf(const std::string &in, int narrow, std::vector<bool> &out,
                      std::string &hri) {
    // ITF encodes digits in pairs, so an odd number of them cannot be printed.
    if (!IsDigits(in) || (in.size() % 2) != 0) return false;

    int wide = WideWidth(narrow);
    AppendNW(out, "nnnn", narrow); // start

    for (size_t i = 0; i + 1 < in.size(); i += 2) {
        const char *bars = ITF_PATTERNS[in[i] - '0'];
        const char *spaces = ITF_PATTERNS[in[i + 1] - '0'];
        for (int e = 0; e < 5; ++e) {
            Run(out, true, bars[e] == 'w' ? wide : narrow);
            Run(out, false, spaces[e] == 'w' ? wide : narrow);
        }
    }

    AppendNW(out, "wnn", narrow); // stop
    hri = in;
    return true;
}

// ---------------------------------------------------------------------------
// CODABAR (NW-7)
// ---------------------------------------------------------------------------

static const char CODABAR_CHARS[] = "0123456789-$:/.+ABCD";
static const char *CODABAR_PATTERNS[] = {
    "nnnnnww", "nnnnwwn", "nnnwnnw", "wwnnnnn", "nnwnnwn", // 0-4
    "wnnnnwn", "nwnnnnw", "nwnnwnn", "nwwnnnn", "wnnwnnn", // 5-9
    "nnnwwnn", "nnwwnnn", "wnnnwnw", "wnwnnnw", "wnwnwnn", // - $ : / .
    "nnwnwnw",                                             // +
    "nnwwnwn", "nwnwnnw", "nnnwnww", "nnnwwwn"             // A B C D
};

static bool EncodeCodabar(const std::string &in, int narrow,
                          std::vector<bool> &out, std::string &hri) {
    if (in.size() < 3) return false;

    // Start and stop must be one of A-D (lower case is accepted too).
    std::string d = in;
    for (size_t i = 0; i < d.size(); ++i) {
        if (d[i] >= 'a' && d[i] <= 'd') d[i] = (char)(d[i] - 'a' + 'A');
    }
    if (!strchr("ABCD", d[0]) || !strchr("ABCD", d[d.size() - 1])) return false;

    for (size_t i = 0; i < d.size(); ++i) {
        const char *pos = strchr(CODABAR_CHARS, d[i]);
        if (!pos || d[i] == '\0') return false;
        // Only the first and last character may be a start/stop symbol.
        size_t index = (size_t)(pos - CODABAR_CHARS);
        if (index >= 16 && i != 0 && i != d.size() - 1) return false;
        if (i > 0) Run(out, false, narrow); // inter-character gap
        AppendNW(out, CODABAR_PATTERNS[index], narrow);
    }
    hri = d;
    return true;
}

// ---------------------------------------------------------------------------
// CODE93
// ---------------------------------------------------------------------------

// Values 0..42 are the base character set, 43..46 the four shift symbols
// ($), (%), (/) and (+), and 47 is the start/stop symbol.
static const char *CODE93_PATTERNS[48] = {
    "100010100", "101001000", "101000100", "101000010", "100101000", // 0-4
    "100100100", "100100010", "101010000", "100010010", "100001010", // 5-9
    "110101000", "110100100", "110100010", "110010100", "110010010", // A-E
    "110001010", "101101000", "101100100", "101100010", "100110100", // F-J
    "100011010", "101011000", "101001100", "101000110", "100101100", // K-O
    "100010110", "110110100", "110110010", "110101100", "110100110", // P-T
    "110010110", "110011010", "101101100", "101100110", "100110110", // U-Y
    "100111010",                                                     // Z
    "100101110", "111010100", "111010010", "111001010", "101101110", // - . SP $ /
    "101110110", "110101110",                                        // + %
    "100100110", "111011010", "111010110", "100110010",              // shifts
    "101011110"                                                      // start/stop
};

static const int CODE93_SHIFT_DOLLAR = 43;
static const int CODE93_SHIFT_PERCENT = 44;
static const int CODE93_SHIFT_SLASH = 45;
static const int CODE93_SHIFT_PLUS = 46;
static const int CODE93_START = 47;

// Base-set value of a character that CODE93 can encode directly.
static int Code93Base(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c == '-') return 36;
    if (c == '.') return 37;
    if (c == ' ') return 38;
    return -1;
}

// Expands one byte into the one or two CODE93 values that represent it, using
// the standard full-ASCII shift encoding.
static bool Code93Values(unsigned char c, std::vector<int> &values) {
    if (c > 127) return false;
    if (c == 0) {
        values.push_back(CODE93_SHIFT_PERCENT);
        values.push_back(Code93Base('U'));
    } else if (c <= 26) {
        values.push_back(CODE93_SHIFT_DOLLAR);
        values.push_back(Code93Base('A') + (c - 1));
    } else if (c <= 31) {
        values.push_back(CODE93_SHIFT_PERCENT);
        values.push_back(Code93Base('A') + (c - 27));
    } else if (c == ' ' || c == '-' || c == '.' ||
               (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
        values.push_back(Code93Base((char)c));
    } else if (c >= 33 && c <= 44) { // ! " # $ % & ' ( ) * + ,
        values.push_back(CODE93_SHIFT_SLASH);
        values.push_back(Code93Base('A') + (c - 33));
    } else if (c == '/') {
        values.push_back(CODE93_SHIFT_SLASH);
        values.push_back(Code93Base('O'));
    } else if (c == ':') {
        values.push_back(CODE93_SHIFT_SLASH);
        values.push_back(Code93Base('Z'));
    } else if (c >= ';' && c <= '?') {
        values.push_back(CODE93_SHIFT_PERCENT);
        values.push_back(Code93Base('F') + (c - ';'));
    } else if (c == '@') {
        values.push_back(CODE93_SHIFT_PERCENT);
        values.push_back(Code93Base('V'));
    } else if (c >= '[' && c <= '_') {
        values.push_back(CODE93_SHIFT_PERCENT);
        values.push_back(Code93Base('K') + (c - '['));
    } else if (c == '`') {
        values.push_back(CODE93_SHIFT_PERCENT);
        values.push_back(Code93Base('W'));
    } else if (c >= 'a' && c <= 'z') {
        values.push_back(CODE93_SHIFT_PLUS);
        values.push_back(Code93Base('A') + (c - 'a'));
    } else if (c >= '{' && c <= 127) {
        values.push_back(CODE93_SHIFT_PERCENT);
        values.push_back(Code93Base('P') + (c - '{'));
    } else {
        return false;
    }
    return true;
}

static int Code93Check(const std::vector<int> &values, int maxWeight) {
    int sum = 0;
    int weight = 1;
    for (int i = (int)values.size() - 1; i >= 0; --i) {
        sum += values[i] * weight;
        if (++weight > maxWeight) weight = 1;
    }
    return sum % 47;
}

static bool EncodeCode93(const std::vector<unsigned char> &data, int narrow,
                         std::vector<bool> &out, std::string &hri) {
    if (data.empty()) return false;

    std::vector<int> values;
    for (size_t i = 0; i < data.size(); ++i) {
        if (!Code93Values(data[i], values)) return false;
    }

    values.push_back(Code93Check(values, 20)); // check character C
    values.push_back(Code93Check(values, 15)); // check character K

    AppendBits(out, CODE93_PATTERNS[CODE93_START], narrow);
    for (size_t i = 0; i < values.size(); ++i) {
        AppendBits(out, CODE93_PATTERNS[values[i]], narrow);
    }
    AppendBits(out, CODE93_PATTERNS[CODE93_START], narrow);
    Run(out, true, narrow); // terminating bar

    hri.assign(data.begin(), data.end());
    return true;
}

// ---------------------------------------------------------------------------
// CODE128
// ---------------------------------------------------------------------------

static const char *CODE128_PATTERNS[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213",
    "122312", "132212", "221213", "221312", "231212", "112232", "122132",
    "122231", "113222", "123122", "123221", "223211", "221132", "221231",
    "213212", "223112", "312131", "311222", "321122", "321221", "312212",
    "322112", "322211", "212123", "212321", "232121", "111323", "131123",
    "131321", "112313", "132113", "132311", "211313", "231113", "231311",
    "112133", "112331", "132131", "113123", "113321", "133121", "313121",
    "211331", "231131", "213113", "213311", "213131", "311123", "311321",
    "331121", "312113", "312311", "332111", "314111", "221411", "431111",
    "111224", "111422", "121124", "121421", "141122", "141221", "112214",
    "112412", "122114", "122411", "142112", "142211", "241211", "221114",
    "413111", "241112", "134111", "111242", "121142", "121241", "114212",
    "124112", "124211", "411212", "421112", "421211", "212141", "214121",
    "412121", "111143", "111341", "131141", "114113", "114311", "411113",
    "411311", "113141", "114131", "311141", "411131",
    "211412", // 103 START A
    "211214", // 104 START B
    "211232", // 105 START C
    "2331112" // 106 STOP
};

// ESC/POS passes CODE128 data with "{X" escapes; the stream must open with a
// code set selector.
static bool EncodeCode128(const std::vector<unsigned char> &data, int narrow,
                          std::vector<bool> &out, std::string &hri) {
    if (data.size() < 2 || data[0] != '{') return false;

    int codeSet;
    int startValue;
    switch (data[1]) {
    case 'A': codeSet = 'A'; startValue = 103; break;
    case 'B': codeSet = 'B'; startValue = 104; break;
    case 'C': codeSet = 'C'; startValue = 105; break;
    default: return false;
    }

    std::vector<int> values;
    size_t i = 2;
    while (i < data.size()) {
        unsigned char c = data[i];

        if (c == '{' && i + 1 < data.size()) {
            unsigned char esc = data[i + 1];
            i += 2;
            switch (esc) {
            case '{': // literal '{' - only code set B covers 0x7B
                if (codeSet != 'B') return false;
                values.push_back('{' - 0x20);
                hri += '{';
                continue;
            case 'A': values.push_back(101); codeSet = 'A'; continue;
            case 'B': values.push_back(100); codeSet = 'B'; continue;
            case 'C': values.push_back(99);  codeSet = 'C'; continue;
            case 'S': values.push_back(98);  continue; // SHIFT
            case '1': values.push_back(102); continue; // FNC1
            case '2': values.push_back(97);  continue; // FNC2
            case '3': values.push_back(96);  continue; // FNC3
            case '4': // FNC4 shares a value with the code set switches
                if (codeSet == 'A') values.push_back(101);
                else if (codeSet == 'B') values.push_back(100);
                else return false;
                continue;
            default:
                return false;
            }
        }

        if (codeSet == 'C') {
            if (i + 1 >= data.size()) return false;
            unsigned char c2 = data[i + 1];
            if (c < '0' || c > '9' || c2 < '0' || c2 > '9') return false;
            values.push_back((c - '0') * 10 + (c2 - '0'));
            hri += (char)c;
            hri += (char)c2;
            i += 2;
            continue;
        }

        if (codeSet == 'A') {
            if (c >= 0x20 && c <= 0x5F) values.push_back(c - 0x20);
            else if (c <= 0x1F) values.push_back(c + 64);
            else return false;
        } else { // code set B
            if (c >= 0x20 && c <= 0x7F) values.push_back(c - 0x20);
            else return false;
        }
        if (c >= 0x20 && c <= 0x7E) hri += (char)c;
        i++;
    }

    if (values.empty()) return false;

    long sum = startValue;
    for (size_t k = 0; k < values.size(); ++k) {
        sum += (long)(k + 1) * values[k];
    }
    int check = (int)(sum % 103);

    AppendWidths(out, CODE128_PATTERNS[startValue], narrow);
    for (size_t k = 0; k < values.size(); ++k) {
        AppendWidths(out, CODE128_PATTERNS[values[k]], narrow);
    }
    AppendWidths(out, CODE128_PATTERNS[check], narrow);
    AppendWidths(out, CODE128_PATTERNS[106], narrow);
    return true;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

int BarcodeTypeFromM(int m, bool functionA) {
    if (m >= 0 && m <= 6) return m; // both functions
    if (functionA) return -1;       // function A only defines m = 0..6
    if (m >= 65 && m <= 71) return m - 65;
    if (m == 72) return BARCODE_CODE93;
    if (m == 73) return BARCODE_CODE128;
    return -1;
}

bool EncodeBarcode(int type, const std::vector<unsigned char> &data,
                   int moduleWidth, std::vector<bool> &dots, std::string &hri) {
    dots.clear();
    hri.clear();
    if (data.empty()) return false;
    if (moduleWidth < 1) moduleWidth = 1;

    std::string text(data.begin(), data.end());

    switch (type) {
    case BARCODE_UPCA:    return EncodeUpcA(text, moduleWidth, dots, hri);
    case BARCODE_UPCE:    return EncodeUpcE(text, moduleWidth, dots, hri);
    case BARCODE_EAN13:   return EncodeEan13(text, moduleWidth, dots, hri);
    case BARCODE_EAN8:    return EncodeEan8(text, moduleWidth, dots, hri);
    case BARCODE_CODE39:  return EncodeCode39(text, moduleWidth, dots, hri);
    case BARCODE_ITF:     return EncodeItf(text, moduleWidth, dots, hri);
    case BARCODE_CODABAR: return EncodeCodabar(text, moduleWidth, dots, hri);
    case BARCODE_CODE93:  return EncodeCode93(data, moduleWidth, dots, hri);
    case BARCODE_CODE128: return EncodeCode128(data, moduleWidth, dots, hri);
    default: return false;
    }
}
