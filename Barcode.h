#pragma once

#include <string>
#include <vector>

// Symbologies supported by GS k. The values match the `m` parameter of
// function A; function B values (65..73) are mapped onto these by the caller.
enum BarcodeType {
  BARCODE_UPCA = 0,
  BARCODE_UPCE = 1,
  BARCODE_EAN13 = 2,
  BARCODE_EAN8 = 3,
  BARCODE_CODE39 = 4,
  BARCODE_ITF = 5,
  BARCODE_CODABAR = 6,
  BARCODE_CODE93 = 7,
  BARCODE_CODE128 = 8
};

// Translates a GS k `m` parameter into a BarcodeType.
// Returns -1 when m is not a valid symbology selector.
// `functionA` selects the NUL-terminated form, which only accepts m = 0..6.
int BarcodeTypeFromM(int m, bool functionA);

// Encodes `data` into a horizontal run of dots (true = bar, false = space).
//
// `moduleWidth` is the narrow-element width in dots (GS w, normally 2..6), so
// the result is already at printer resolution and needs no further scaling.
//
// `hri` receives the human-readable interpretation, including any check digit
// the encoder had to compute.
//
// Returns false when the data is not valid for the symbology. Real ESC/POS
// printers print nothing in that case, and so does the caller.
bool EncodeBarcode(int type, const std::vector<unsigned char> &data,
                   int moduleWidth, std::vector<bool> &dots, std::string &hri);
