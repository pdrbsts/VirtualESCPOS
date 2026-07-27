#pragma once

#include <vector>

// Error correction level, matching the n of GS ( k <fn=69>: 48..51.
enum QREcLevel {
  QR_ECC_LOW = 0,      // recovers about 7%
  QR_ECC_MEDIUM = 1,   // 15%
  QR_ECC_QUARTILE = 2, // 25%
  QR_ECC_HIGH = 3      // 30%
};

// Encodes `data` as a QR Code (model 2) symbol.
//
// `modules` receives a square matrix indexed [y][x] where true is a dark
// module; the quiet zone is not included. The encoder picks the smallest
// version that fits and the mask with the lowest penalty score, exactly as a
// printer would.
//
// Returns false when the data does not fit even in version 40 at the requested
// error correction level.
bool EncodeQRCode(const std::vector<unsigned char> &data, int ecLevel,
                  std::vector<std::vector<bool> > &modules);
