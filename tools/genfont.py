#!/usr/bin/env python3
"""Generates the dot matrices of ESC/POS Font A (12x24) and Font B (10x24).

A thermal printer draws Font A on a 12 dot wide, 24 dot tall cell, which is
what puts 48 columns on 80 mm paper (48 * 12 = 576 dots = the printable width
at 203 dpi). Font B (ESC M 1) is the narrow one, 10 dots to the column and 57
columns to the line, on a cell just as tall. The renderer needs the same grid
to be faithful, so the glyphs are shipped as a bitmap table rather than asked
from the system's font engine.

Two sources feed each table:

  * Box drawing, block and shade characters are drawn here, by hand, on the
    cell. They have to tile: a horizontal rule must run edge to edge so that
    consecutive cells join up, and the strokes of adjacent characters must line
    up to the dot. A scaled outline font gets this wrong.

  * Everything else is rasterized from DejaVu Sans Mono, whose advance is
    exactly 12 dots at 20 px and exactly 10 at 17 px. The license (Bitstream
    Vera derivative) allows redistribution of derived work.

Both cells are 24 dots tall and both put the baseline on row 19, so a line
that switches fonts still sits on one baseline; Font B is simply the smaller
glyph, three rows short of the cell, the way a printer's own Font B is.

Run from the repository root:

    python tools/genfont.py

The character set is the union of ASCII and every code page table in
CodePages.cpp, so anything ESC t can select has a glyph.
"""

import os
import re
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    "C:/Windows/Fonts/DejavuSansMono-5m7L.ttf",
    "C:/Windows/Fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/Library/Fonts/DejaVuSansMono.ttf",
]

# The row the glyphs stand on, shared by both fonts.
BASELINE = 19

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- The cell --------------------------------------------------------------
# Single strokes sit on the centre of the cell; double strokes straddle it.
# Keeping these as spans (not centres) is what makes the joins line up, and
# deriving them from the cell is what keeps the two fonts drawn the same way.
CELL_W = 0
CELL_H = 0
V_SINGLE = []   # columns of a single vertical rule
V_DOUBLE = []   # columns of a double vertical rule
H_SINGLE = []   # rows of a single horizontal rule
H_DOUBLE = []


def set_cell(w, h):
    global CELL_W, CELL_H, V_SINGLE, V_DOUBLE, H_SINGLE, H_DOUBLE
    CELL_W, CELL_H = w, h
    V_SINGLE = [(w // 2 - 1, w // 2)]
    V_DOUBLE = [(w // 2 - 3, w // 2 - 2), (w // 2 + 1, w // 2 + 2)]
    H_SINGLE = [(h // 2 - 1, h // 2)]
    H_DOUBLE = [(h // 2 - 3, h // 2 - 2), (h // 2 + 1, h // 2 + 2)]


UP, DOWN, LEFT, RIGHT = "UP", "DOWN", "LEFT", "RIGHT"


def blank():
    return [[0] * CELL_W for _ in range(CELL_H)]


def fill(g, r0, r1, c0, c1):
    for y in range(max(0, r0), min(CELL_H - 1, r1) + 1):
        for x in range(max(0, c0), min(CELL_W - 1, c1) + 1):
            g[y][x] = 1


def clear(g, r0, r1, c0, c1):
    for y in range(max(0, r0), min(CELL_H - 1, r1) + 1):
        for x in range(max(0, c0), min(CELL_W - 1, c1) + 1):
            g[y][x] = 0


# --- Box drawing -----------------------------------------------------------

def parse_arms(name):
    """Turns a Unicode box drawing name into the weight of each of the 4 arms.

    The names spell out the shape: "BOX DRAWINGS LIGHT DOWN AND RIGHT" is a
    corner, "BOX DRAWINGS DOWN SINGLE AND RIGHT DOUBLE" is the same corner with
    one arm doubled. Weights are 0 (absent), 1 (single) or 2 (double).
    """
    body = name.replace("BOX DRAWINGS ", "")
    arms = {UP: 0, DOWN: 0, LEFT: 0, RIGHT: 0}

    def add(direction, weight):
        if direction == "HORIZONTAL":
            arms[LEFT] = arms[RIGHT] = weight
        elif direction == "VERTICAL":
            arms[UP] = arms[DOWN] = weight
        else:
            arms[direction] = weight

    if body.startswith("LIGHT ") or body.startswith("DOUBLE "):
        weight, rest = (1, body[6:]) if body.startswith("LIGHT ") else (2, body[7:])
        for part in rest.split(" AND "):
            add(part.strip(), weight)
    else:
        # "<DIRECTION> SINGLE AND <DIRECTION> DOUBLE" and friends.
        for part in body.split(" AND "):
            direction, _, weight_word = part.strip().rpartition(" ")
            add(direction, 1 if weight_word == "SINGLE" else 2)
    return arms


def strokes(weight, single, double):
    return list(single if weight == 1 else double) if weight else []


def draw_box(name):
    arms = parse_arms(name)
    vw = max(arms[UP], arms[DOWN])
    hw = max(arms[LEFT], arms[RIGHT])
    vs = strokes(vw, V_SINGLE, V_DOUBLE)   # ordered left to right
    hs = strokes(hw, H_SINGLE, H_DOUBLE)   # ordered top to bottom
    g = blank()

    through_v = arms[UP] > 0 and arms[DOWN] > 0
    through_h = arms[LEFT] > 0 and arms[RIGHT] > 0
    corner = (arms[UP] > 0) != (arms[DOWN] > 0) and (arms[LEFT] > 0) != (arms[RIGHT] > 0)

    # Order each family from the far side of the arm to the near side, so
    # index 0 is always the "outer" stroke of a corner.
    def ordered(seq, reverse):
        return list(reversed(seq)) if reverse else list(seq)

    # For a DOWN arm the outermost horizontal is the topmost one, and so on.
    h_outer_first = ordered(hs, arms[UP] > 0 and arms[DOWN] == 0)
    v_outer_first = ordered(vs, arms[LEFT] > 0 and arms[RIGHT] == 0)

    def h_edge(stroke, towards_down):
        """The row of `stroke` a vertical arm growing `towards_down` starts on."""
        return stroke[0] if towards_down else stroke[1]

    def v_edge(stroke, towards_right):
        return stroke[0] if towards_right else stroke[1]

    # --- vertical strokes ---
    for i, (c0, c1) in enumerate(vs):
        spans = []
        if through_v:
            spans.append((0, CELL_H - 1))
        elif vw:
            down = arms[DOWN] > 0
            if not hs:
                spans.append((0, CELL_H - 1))
            else:
                if corner and vw == 2 and hw == 2:
                    # Nested L shapes: outer meets outer, inner meets inner.
                    idx = v_outer_first.index((c0, c1))
                    start = h_edge(h_outer_first[idx], down)
                elif corner:
                    start = h_edge(h_outer_first[0], down)
                else:
                    # The arm grows away from the rule it springs from, so it
                    # starts on the nearest stroke of the other family.
                    start = h_edge(h_outer_first[-1], down)
                spans.append((start, CELL_H - 1) if down else (0, start))
        for r0, r1 in spans:
            fill(g, r0, r1, c0, c1)

        # A double rule crossing a double rule leaves the pocket between the
        # two lines open, so the stroke is cut where the other pair passes.
        if vw == 2 and hw == 2:
            outward_arm = arms[LEFT] if i == 0 else arms[RIGHT]
            if outward_arm and through_v:
                clear(g, hs[0][1] + 1, hs[1][0] - 1, c0, c1)

    # --- horizontal strokes ---
    for i, (r0, r1) in enumerate(hs):
        spans = []
        if through_h:
            spans.append((0, CELL_W - 1))
        elif hw:
            right = arms[RIGHT] > 0
            if not vs:
                spans.append((0, CELL_W - 1))
            else:
                if corner and vw == 2 and hw == 2:
                    idx = h_outer_first.index((r0, r1))
                    start = v_edge(v_outer_first[idx], right)
                elif corner:
                    start = v_edge(v_outer_first[0], right)
                else:
                    start = v_edge(v_outer_first[-1], right)
                spans.append((start, CELL_W - 1) if right else (0, start))
        for c0, c1 in spans:
            fill(g, r0, r1, c0, c1)

        if vw == 2 and hw == 2:
            outward_arm = arms[UP] if i == 0 else arms[DOWN]
            if outward_arm and through_h:
                clear(g, r0, r1, vs[0][1] + 1, vs[1][0] - 1)

    return g


def draw_shade(numerator):
    """Ordered dither at 1/4, 1/2 or 3/4 coverage, on a 2x2 grid."""
    g = blank()
    for y in range(CELL_H):
        for x in range(CELL_W):
            if numerator == 1:
                on = x % 2 == 0 and y % 2 == 0
            elif numerator == 2:
                on = (x + y) % 2 == 0
            else:
                on = not (x % 2 == 1 and y % 2 == 1)
            g[y][x] = 1 if on else 0
    return g


def draw_block(cp):
    g = blank()
    if cp == 0x2580:
        fill(g, 0, CELL_H // 2 - 1, 0, CELL_W - 1)
    elif cp == 0x2584:
        fill(g, CELL_H // 2, CELL_H - 1, 0, CELL_W - 1)
    elif cp == 0x2588:
        fill(g, 0, CELL_H - 1, 0, CELL_W - 1)
    elif cp == 0x258C:
        fill(g, 0, CELL_H - 1, 0, CELL_W // 2 - 1)
    elif cp == 0x2590:
        fill(g, 0, CELL_H - 1, CELL_W // 2, CELL_W - 1)
    elif cp == 0x25A0:
        side = 8                      # a square that reads as square
        left = (CELL_W - side) // 2
        fill(g, 8, 15, left, left + side - 1)
    return g


def draw_replacement():
    """U+FFFD, so a byte with no mapping shows up instead of vanishing."""
    g = blank()
    fill(g, 4, 19, 1, CELL_W - 2)
    clear(g, 6, 17, 3, CELL_W - 4)
    return g


PROCEDURAL_BLOCKS = {0x2580, 0x2584, 0x2588, 0x258C, 0x2590, 0x25A0}
PROCEDURAL_SHADES = {0x2591: 1, 0x2592: 2, 0x2593: 3}


def procedural(cp):
    if 0x2500 <= cp <= 0x257F:
        return draw_box(unicodedata.name(chr(cp)))
    if cp in PROCEDURAL_SHADES:
        return draw_shade(PROCEDURAL_SHADES[cp])
    if cp in PROCEDURAL_BLOCKS:
        return draw_block(cp)
    if cp == 0xFFFD:
        return draw_replacement()
    return None


# --- Outline rasterization -------------------------------------------------

def load_font(size):
    """Opens the source font and checks it really is the cell it claims to be.

    The advance has to be the full cell width - that is what makes the table a
    fixed pitch font - and the glyph has to fit between the top of the cell and
    its last row once it is stood on the baseline.
    """
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            font = ImageFont.truetype(path, size)
            ascent, descent = font.getmetrics()
            if (round(font.getlength("M")) != CELL_W or ascent > BASELINE
                    or BASELINE + descent > CELL_H):
                sys.exit("%s does not fit the %dx%d cell at size %d "
                         "(ascent %d, descent %d, advance %.2f)"
                         % (path, CELL_W, CELL_H, size, ascent, descent,
                            font.getlength("M")))
            return font, path
    sys.exit("DejaVu Sans Mono not found; tried:\n  " + "\n  ".join(FONT_CANDIDATES))


PAD = 12


def nudge(lo, hi, size):
    """How far to shift ink spanning [lo, hi] to bring it inside [0, size)."""
    if hi - lo + 1 <= size:
        if lo < 0:
            return -lo
        if hi > size - 1:
            return size - 1 - hi
        return 0
    # Too big to fit whatever we do: centre it and lose as little as possible
    # from each end.
    return -lo - (hi - lo + 1 - size) // 2


def rasterize(font, cp):
    """Renders one character onto the cell, with the baseline on row 19.

    Pillow's default anchor puts the ascender line on the y it is given, so
    drawing that many rows above the baseline lands the glyph exactly where the
    cell wants it - which is how Font B, whose glyph is shorter than the cell,
    still comes out standing on Font A's baseline. Rendering into a 1-bit image
    keeps FreeType in monochrome mode: the result is already a dot pattern,
    with no grey to threshold.
    """
    ascent, _ = font.getmetrics()
    img = Image.new("1", (CELL_W + 2 * PAD, CELL_H + 2 * PAD), 0)
    ImageDraw.Draw(img).text((PAD, PAD + BASELINE - ascent), chr(cp), font=font, fill=1)
    px = img.load()

    dots = [(y - PAD, x - PAD)
            for y in range(img.height) for x in range(img.width) if px[x, y]]
    if not dots:
        return blank(), False

    # A few glyphs poke a dot or two past the cell - R's leg, the ring on U -
    # because the outline is drawn to an advance of 12 with no room to spare.
    # Nudging the whole glyph back inside keeps it whole; a dot cut off the
    # side of an R is far more visible than the same R sitting one dot over.
    rows = [d[0] for d in dots]
    cols = [d[1] for d in dots]
    dx = nudge(min(cols), max(cols), CELL_W)
    dy = nudge(min(rows), max(rows), CELL_H)

    g = blank()
    overflow = False
    for cy, cx in dots:
        cy += dy
        cx += dx
        if 0 <= cy < CELL_H and 0 <= cx < CELL_W:
            g[cy][cx] = 1
        else:
            overflow = True
    return g, overflow


def is_blank(g):
    return not any(any(row) for row in g)


# --- Character set ---------------------------------------------------------

def charset():
    src = open(os.path.join(ROOT, "CodePages.cpp"), encoding="utf-8").read()
    tables = re.findall(r"static const wchar_t \w+\[128\] = \{(.*?)\};", src, re.S)
    if len(tables) < 10:
        sys.exit("could not read the code page tables from CodePages.cpp")
    codes = set(range(0x20, 0x7F))
    for body in tables:
        codes.update(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", body))
    codes.add(0xFFFD)   # always give unmapped bytes something to show
    return sorted(codes)


# --- Emitting --------------------------------------------------------------

HEADER = """#pragma once

// ESC/POS Font %(letter)s, as the %(w)d x %(h)d dot cell a thermal printer actually uses.
//
%(intro)s

const int FONT_%(letter)s_WIDTH = %(w)d;
const int FONT_%(letter)s_HEIGHT = %(h)d;

// The %(h)d rows of `ch`, one 16-bit word each, bit %(msb)d being the leftmost dot.
// Returns null for a character the table has no glyph for.
const unsigned short *Font%(letter)sGlyph(wchar_t ch);
"""

SOURCE_HEAD = """#include "%(basename)s.h"

#include <cstddef>

// Generated by tools/genfont.py - do not edit by hand.
//
// Box drawing, block and shade characters are drawn on the grid so that they
// tile; the rest are rasterized from DejaVu Sans Mono at %(size)d px, where the
%(source_note)s
//
// Rows run top to bottom, bit %(msb)d is the leftmost dot, and the baseline is
// row %(baseline)d.

namespace {

struct Glyph {
  unsigned short code;
  unsigned short rows[FONT_%(letter)s_HEIGHT];
};

// Sorted by code point: Font%(letter)sGlyph binary searches it.
const Glyph kGlyphs[] = {
"""

SOURCE_TAIL = """};

const size_t kGlyphCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

} // namespace

const unsigned short *Font%(letter)sGlyph(wchar_t ch) {
  unsigned int code = (unsigned int)ch;
  if (code > 0xFFFF) return NULL;
  size_t lo = 0, hi = kGlyphCount;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (kGlyphs[mid].code < code) lo = mid + 1;
    else hi = mid;
  }
  if (lo < kGlyphCount && kGlyphs[lo].code == code) return kGlyphs[lo].rows;
  return NULL;
}
"""

# The two fonts, and the prose that goes with each: what the cell is for, and
# what the source font does at that size.
FONTS = [
    dict(
        letter="A", w=12, h=24, size=20, basename="FontA12x24",
        intro="""// 48 columns of 12 dots is the 576 dot printable width of 80 mm paper at
// 203 dpi, so a receipt only lines up if the glyphs live on this grid. The
// table is generated by tools/genfont.py - edit that, not this file.""",
        source_note="""// advance is exactly 12 dots and ascent + descent exactly 24.""",
    ),
    dict(
        letter="B", w=10, h=24, size=17, basename="FontB10x24",
        intro="""// ESC M 1 selects it: at 10 dots to the column 57 of them fit the 576 dot
// printable width of 80 mm paper at 203 dpi. The cell is as tall as Font A's
// and stands on the same baseline, so a line that mixes the two lines up. The
// table is generated by tools/genfont.py - edit that, not this file.""",
        source_note="""// advance is exactly 10 dots and ascent + descent 21 - the glyph is smaller
// than Font A's, not squeezed, which is what Font B is on the printer too.""",
    ),
]


def to_words(g):
    words = []
    for row in g:
        w = 0
        for x in range(CELL_W):
            if row[x]:
                w |= 1 << (CELL_W - 1 - x)
        words.append(w)
    return words


def emit(spec, glyphs):
    fields = dict(spec, msb=spec["w"] - 1, baseline=BASELINE)

    with open(os.path.join(ROOT, spec["basename"] + ".h"), "w",
              encoding="utf-8", newline="\n") as f:
        f.write(HEADER % fields)

    out = [SOURCE_HEAD % fields]
    for cp, words in glyphs:
        try:
            label = unicodedata.name(chr(cp))
        except ValueError:
            label = "<unnamed>"
        out.append("    // U+%04X %s\n" % (cp, label))
        out.append("    {0x%04X, {" % cp)
        for i, w in enumerate(words):
            if i % 8 == 0:
                out.append("\n     ")
            out.append("0x%03X, " % w)
        out.append("}},\n")
    out.append(SOURCE_TAIL % fields)

    with open(os.path.join(ROOT, spec["basename"] + ".cpp"), "w",
              encoding="utf-8", newline="\n") as f:
        f.write("".join(out))


def generate(spec, codes):
    set_cell(spec["w"], spec["h"])
    font, font_path = load_font(spec["size"])
    notdef, _ = rasterize(font, 0xE000)   # private use: renders as .notdef

    glyphs = []
    missing, clipped = [], []
    for cp in codes:
        g = procedural(cp)
        if g is None:
            g, overflow = rasterize(font, cp)
            if g == notdef and not is_blank(notdef):
                missing.append(cp)
                continue
            if overflow:
                clipped.append(cp)
        glyphs.append((cp, to_words(g)))

    emit(spec, glyphs)
    print("Font %s (%dx%d) from %s at %d px: %d glyphs"
          % (spec["letter"], spec["w"], spec["h"], font_path, spec["size"], len(glyphs)))
    if clipped:
        print("  clipped to the cell: " + " ".join("U+%04X" % c for c in clipped))
    if missing:
        print("  no glyph in the font: " + " ".join("U+%04X" % c for c in missing))


def main():
    codes = charset()
    for spec in FONTS:
        generate(spec, codes)


if __name__ == "__main__":
    main()
