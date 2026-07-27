#include "QRCode.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// QR Code model 2 encoder (ISO/IEC 18004).
//
// The flow is the standard one: pick the tightest encoding mode for the data,
// find the smallest version it fits in, build the bit stream, split it into
// blocks with Reed-Solomon parity, lay the codewords out in the zigzag order,
// then try all eight data masks and keep the one with the lowest penalty.
// ---------------------------------------------------------------------------

namespace {

const int MIN_VERSION = 1;
const int MAX_VERSION = 40;

// Error correction codewords per block, indexed [ecLevel][version].
const signed char ECC_CODEWORDS_PER_BLOCK[4][41] = {
    // 0  1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38  39  40
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // Low
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28}, // Medium
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // Quartile
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // High
};

// Number of error correction blocks, indexed [ecLevel][version].
const signed char NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
    // 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25}, // Low
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49}, // Medium
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68}, // Quartile
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81}, // High
};

// Format-information bit patterns for each EC level (not the enum order).
const int ECC_FORMAT_BITS[4] = {1, 0, 3, 2};

const char *ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

enum Mode { MODE_NUMERIC, MODE_ALPHANUMERIC, MODE_BYTE };

bool GetBit(long x, int i) { return ((x >> i) & 1) != 0; }

// Total modules available for data and error correction, before the function
// patterns take their share.
int GetNumRawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}

int GetNumDataCodewords(int ver, int ecl) {
    return GetNumRawDataModules(ver) / 8 -
           ECC_CODEWORDS_PER_BLOCK[ecl][ver] * NUM_ERROR_CORRECTION_BLOCKS[ecl][ver];
}

// --- Reed-Solomon over GF(256) with primitive polynomial 0x11D --------------

unsigned char RsMultiply(unsigned char x, unsigned char y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (unsigned char)z;
}

std::vector<unsigned char> RsComputeDivisor(int degree) {
    std::vector<unsigned char> result(degree, 0);
    result[degree - 1] = 1;
    unsigned char root = 1;
    for (int i = 0; i < degree; i++) {
        for (size_t j = 0; j < result.size(); j++) {
            result[j] = RsMultiply(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = RsMultiply(root, 0x02);
    }
    return result;
}

std::vector<unsigned char> RsComputeRemainder(const std::vector<unsigned char> &data,
                                              const std::vector<unsigned char> &divisor) {
    std::vector<unsigned char> result(divisor.size(), 0);
    for (size_t i = 0; i < data.size(); i++) {
        unsigned char factor = data[i] ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t j = 0; j < result.size(); j++) {
            result[j] ^= RsMultiply(divisor[j], factor);
        }
    }
    return result;
}

// --- Segment encoding -------------------------------------------------------

int AlphanumericValue(unsigned char c) {
    const char *p = strchr(ALPHANUMERIC_CHARSET, c);
    return (p && c != '\0') ? (int)(p - ALPHANUMERIC_CHARSET) : -1;
}

Mode ChooseMode(const std::vector<unsigned char> &data) {
    bool numeric = true, alnum = true;
    for (size_t i = 0; i < data.size(); i++) {
        if (data[i] < '0' || data[i] > '9') numeric = false;
        if (AlphanumericValue(data[i]) < 0) alnum = false;
    }
    if (numeric) return MODE_NUMERIC;
    if (alnum) return MODE_ALPHANUMERIC;
    return MODE_BYTE;
}

int ModeIndicator(Mode mode) {
    if (mode == MODE_NUMERIC) return 1;
    if (mode == MODE_ALPHANUMERIC) return 2;
    return 4;
}

int CharCountBits(Mode mode, int ver) {
    static const int NUMERIC[3] = {10, 12, 14};
    static const int ALNUM[3] = {9, 11, 13};
    static const int BYTE[3] = {8, 16, 16};
    int tier = (ver <= 9) ? 0 : (ver <= 26 ? 1 : 2);
    if (mode == MODE_NUMERIC) return NUMERIC[tier];
    if (mode == MODE_ALPHANUMERIC) return ALNUM[tier];
    return BYTE[tier];
}

void AppendBits(std::vector<bool> &bits, unsigned int value, int count) {
    for (int i = count - 1; i >= 0; i--) {
        bits.push_back(((value >> i) & 1) != 0);
    }
}

void AppendSegmentData(std::vector<bool> &bits, Mode mode,
                       const std::vector<unsigned char> &data) {
    if (mode == MODE_NUMERIC) {
        for (size_t i = 0; i < data.size();) {
            size_t n = std::min<size_t>(3, data.size() - i);
            unsigned int value = 0;
            for (size_t j = 0; j < n; j++) value = value * 10 + (data[i + j] - '0');
            AppendBits(bits, value, (int)n * 3 + 1);
            i += n;
        }
    } else if (mode == MODE_ALPHANUMERIC) {
        for (size_t i = 0; i < data.size();) {
            if (i + 1 < data.size()) {
                unsigned int value = AlphanumericValue(data[i]) * 45 +
                                     AlphanumericValue(data[i + 1]);
                AppendBits(bits, value, 11);
                i += 2;
            } else {
                AppendBits(bits, AlphanumericValue(data[i]), 6);
                i += 1;
            }
        }
    } else {
        for (size_t i = 0; i < data.size(); i++) AppendBits(bits, data[i], 8);
    }
}

// --- Symbol drawing ---------------------------------------------------------

struct QRSymbol {
    int size;
    int version;
    int ecl;
    std::vector<std::vector<bool> > modules;
    std::vector<std::vector<bool> > isFunction;

    QRSymbol(int ver, int eccLevel)
        : size(ver * 4 + 17), version(ver), ecl(eccLevel),
          modules(size, std::vector<bool>(size, false)),
          isFunction(size, std::vector<bool>(size, false)) {}

    void SetFunctionModule(int x, int y, bool isDark) {
        if (x < 0 || x >= size || y < 0 || y >= size) return;
        modules[y][x] = isDark;
        isFunction[y][x] = true;
    }

    void DrawFinderPattern(int x, int y) {
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                int dist = std::max(std::abs(dx), std::abs(dy));
                SetFunctionModule(x + dx, y + dy, dist != 2 && dist != 4);
            }
        }
    }

    void DrawAlignmentPattern(int x, int y) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                SetFunctionModule(x + dx, y + dy,
                                  std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        }
    }

    std::vector<int> AlignmentPatternPositions() const {
        std::vector<int> result;
        if (version == 1) return result;
        int numAlign = version / 7 + 2;
        int step = (version == 32) ? 26
                                   : (version * 4 + numAlign * 2 + 1) /
                                         (numAlign * 2 - 2) * 2;
        for (int i = 0, pos = version * 4 + 10; i < numAlign - 1; i++, pos -= step) {
            result.insert(result.begin(), pos);
        }
        result.insert(result.begin(), 6);
        return result;
    }

    void DrawFormatBits(int mask) {
        int data = ECC_FORMAT_BITS[ecl] << 3 | mask;
        int rem = data;
        for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412;

        for (int i = 0; i <= 5; i++) SetFunctionModule(8, i, GetBit(bits, i));
        SetFunctionModule(8, 7, GetBit(bits, 6));
        SetFunctionModule(8, 8, GetBit(bits, 7));
        SetFunctionModule(7, 8, GetBit(bits, 8));
        for (int i = 9; i < 15; i++) SetFunctionModule(14 - i, 8, GetBit(bits, i));

        for (int i = 0; i < 8; i++) SetFunctionModule(size - 1 - i, 8, GetBit(bits, i));
        for (int i = 8; i < 15; i++) SetFunctionModule(8, size - 15 + i, GetBit(bits, i));
        SetFunctionModule(8, size - 8, true); // always dark
    }

    void DrawVersionBits() {
        if (version < 7) return;
        int rem = version;
        for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = (long)version << 12 | rem;
        for (int i = 0; i < 18; i++) {
            bool bit = GetBit(bits, i);
            int a = size - 11 + i % 3;
            int b = i / 3;
            SetFunctionModule(a, b, bit);
            SetFunctionModule(b, a, bit);
        }
    }

    void DrawFunctionPatterns() {
        for (int i = 0; i < size; i++) {
            SetFunctionModule(6, i, i % 2 == 0);
            SetFunctionModule(i, 6, i % 2 == 0);
        }
        DrawFinderPattern(3, 3);
        DrawFinderPattern(size - 4, 3);
        DrawFinderPattern(3, size - 4);

        std::vector<int> pos = AlignmentPatternPositions();
        int numAlign = (int)pos.size();
        for (int i = 0; i < numAlign; i++) {
            for (int j = 0; j < numAlign; j++) {
                // The three finder corners already own those cells.
                if ((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) ||
                    (i == numAlign - 1 && j == 0))
                    continue;
                DrawAlignmentPattern(pos[i], pos[j]);
            }
        }

        DrawFormatBits(0); // placeholder; redrawn once the mask is chosen
        DrawVersionBits();
    }

    void DrawCodewords(const std::vector<unsigned char> &codewords) {
        size_t i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5; // the vertical timing pattern is skipped
            for (int vert = 0; vert < size; vert++) {
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? size - 1 - vert : vert;
                    if (!isFunction[y][x] && i < codewords.size() * 8) {
                        modules[y][x] = GetBit(codewords[i >> 3], 7 - (int)(i & 7));
                        i++;
                    }
                }
            }
        }
    }

    void ApplyMask(int mask) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                bool invert = false;
                switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (invert && !isFunction[y][x]) modules[y][x] = !modules[y][x];
            }
        }
    }

    void FinderPenaltyAddHistory(int runLength, int history[7]) const {
        if (history[0] == 0) runLength += size; // count the light quiet zone
        for (int i = 6; i > 0; i--) history[i] = history[i - 1];
        history[0] = runLength;
    }

    int FinderPenaltyCountPatterns(const int history[7]) const {
        int n = history[1];
        bool core = n > 0 && history[2] == n && history[3] == n * 3 &&
                    history[4] == n && history[5] == n;
        return (core && history[0] >= n * 4 && history[6] >= n ? 1 : 0) +
               (core && history[6] >= n * 4 && history[0] >= n ? 1 : 0);
    }

    int FinderPenaltyTerminateAndCount(bool runColor, int runLength, int history[7]) const {
        if (runColor) {
            FinderPenaltyAddHistory(runLength, history);
            runLength = 0;
        }
        runLength += size;
        FinderPenaltyAddHistory(runLength, history);
        return FinderPenaltyCountPatterns(history);
    }

    long GetPenaltyScore() const {
        const int N1 = 3, N2 = 3, N3 = 40, N4 = 10;
        long result = 0;

        // Rule 1/3 along rows
        for (int y = 0; y < size; y++) {
            bool runColor = false;
            int runLen = 0;
            int history[7] = {0};
            for (int x = 0; x < size; x++) {
                if (modules[y][x] == runColor) {
                    runLen++;
                    if (runLen == 5) result += N1;
                    else if (runLen > 5) result++;
                } else {
                    FinderPenaltyAddHistory(runLen, history);
                    if (!runColor) result += FinderPenaltyCountPatterns(history) * N3;
                    runColor = modules[y][x];
                    runLen = 1;
                }
            }
            result += FinderPenaltyTerminateAndCount(runColor, runLen, history) * N3;
        }
        // Rule 1/3 along columns
        for (int x = 0; x < size; x++) {
            bool runColor = false;
            int runLen = 0;
            int history[7] = {0};
            for (int y = 0; y < size; y++) {
                if (modules[y][x] == runColor) {
                    runLen++;
                    if (runLen == 5) result += N1;
                    else if (runLen > 5) result++;
                } else {
                    FinderPenaltyAddHistory(runLen, history);
                    if (!runColor) result += FinderPenaltyCountPatterns(history) * N3;
                    runColor = modules[y][x];
                    runLen = 1;
                }
            }
            result += FinderPenaltyTerminateAndCount(runColor, runLen, history) * N3;
        }

        // Rule 2: blocks of the same colour
        for (int y = 0; y < size - 1; y++) {
            for (int x = 0; x < size - 1; x++) {
                bool color = modules[y][x];
                if (color == modules[y][x + 1] && color == modules[y + 1][x] &&
                    color == modules[y + 1][x + 1])
                    result += N2;
            }
        }

        // Rule 4: balance of dark and light modules
        long dark = 0;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (modules[y][x]) dark++;
            }
        }
        long total = (long)size * size;
        long k = (std::labs(dark * 20 - total * 10) + total - 1) / total - 1;
        result += k * N4;
        return result;
    }
};

std::vector<unsigned char> AddEccAndInterleave(const std::vector<unsigned char> &data,
                                               int ver, int ecl) {
    int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[ecl][ver];
    int blockEccLen = ECC_CODEWORDS_PER_BLOCK[ecl][ver];
    int rawCodewords = GetNumRawDataModules(ver) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    std::vector<std::vector<unsigned char> > blocks;
    std::vector<unsigned char> rsDiv = RsComputeDivisor(blockEccLen);
    for (int i = 0, k = 0; i < numBlocks; i++) {
        int datLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        std::vector<unsigned char> dat(data.begin() + k, data.begin() + k + datLen);
        k += datLen;
        std::vector<unsigned char> ecc = RsComputeRemainder(dat, rsDiv);
        // Short blocks get a placeholder so every block has the same length
        // while interleaving; it is skipped on the way out.
        if (i < numShortBlocks) dat.push_back(0);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(dat);
    }

    std::vector<unsigned char> result;
    for (size_t i = 0; i < blocks[0].size(); i++) {
        for (size_t j = 0; j < blocks.size(); j++) {
            if (i != (size_t)(shortBlockLen - blockEccLen) || j >= (size_t)numShortBlocks) {
                result.push_back(blocks[j][i]);
            }
        }
    }
    return result;
}

} // namespace

bool EncodeQRCode(const std::vector<unsigned char> &data, int ecLevel,
                  std::vector<std::vector<bool> > &modules) {
    modules.clear();
    if (ecLevel < 0 || ecLevel > 3) ecLevel = QR_ECC_MEDIUM;
    if (data.empty()) return false;

    Mode mode = ChooseMode(data);

    // Smallest version the data fits in. The character count indicator grows
    // with the version, so the bit stream is rebuilt for each candidate.
    int version = 0;
    std::vector<bool> bits;
    for (int ver = MIN_VERSION; ver <= MAX_VERSION; ver++) {
        std::vector<bool> candidate;
        AppendBits(candidate, ModeIndicator(mode), 4);
        AppendBits(candidate, (unsigned int)data.size(), CharCountBits(mode, ver));
        AppendSegmentData(candidate, mode, data);
        if ((int)candidate.size() <= GetNumDataCodewords(ver, ecLevel) * 8) {
            version = ver;
            bits.swap(candidate);
            break;
        }
    }
    if (version == 0) return false;

    int dataCapacityBits = GetNumDataCodewords(version, ecLevel) * 8;

    // Terminator, then pad to a whole codeword, then the alternating pad bytes.
    for (int i = 0; i < 4 && (int)bits.size() < dataCapacityBits; i++) {
        bits.push_back(false);
    }
    while (bits.size() % 8 != 0) bits.push_back(false);
    for (unsigned char pad = 0xEC; (int)bits.size() < dataCapacityBits;
         pad = (unsigned char)(pad ^ 0xEC ^ 0x11)) {
        AppendBits(bits, pad, 8);
    }

    std::vector<unsigned char> dataCodewords(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); i++) {
        if (bits[i]) dataCodewords[i >> 3] |= (unsigned char)(1 << (7 - (i & 7)));
    }

    QRSymbol symbol(version, ecLevel);
    symbol.DrawFunctionPatterns();
    symbol.DrawCodewords(AddEccAndInterleave(dataCodewords, version, ecLevel));

    // Pick the mask with the lowest penalty, as the standard requires.
    int bestMask = 0;
    long minPenalty = -1;
    for (int mask = 0; mask < 8; mask++) {
        symbol.ApplyMask(mask);
        symbol.DrawFormatBits(mask);
        long penalty = symbol.GetPenaltyScore();
        if (minPenalty < 0 || penalty < minPenalty) {
            minPenalty = penalty;
            bestMask = mask;
        }
        symbol.ApplyMask(mask); // masking is its own inverse
    }
    symbol.ApplyMask(bestMask);
    symbol.DrawFormatBits(bestMask);

    modules = symbol.modules;
    return true;
}
