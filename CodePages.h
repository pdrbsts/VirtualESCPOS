#pragma once

// Character code tables selected by ESC t n.
//
// A receipt printer holds one 8-bit code page at a time: bytes below 0x80 are
// ASCII and the upper half is whatever ESC t selected. Getting this wrong is
// what turns "Ação" into "AþÒo", so the tables are generated from the reference
// encodings rather than written out by hand.

// Maps one byte to Unicode using the given ESC t code page. Unknown code pages
// fall back to PC437, which is what printers power up with.
wchar_t MapCodePageChar(unsigned char c, int codePage);

// True when the given ESC t parameter names a code page we have a table for.
bool IsKnownCodePage(int codePage);
