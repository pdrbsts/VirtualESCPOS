#import "PrinterView.h"
#include "../VirtualPrinter.h"

@implementation PrinterView {
  VirtualPrinter *_printer;
  CGFloat _totalHeight;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self) {
    _totalHeight = 100;
  }
  return self;
}

- (void)setPrinter:(VirtualPrinter *)printerObj {
  _printer = printerObj;
}

- (BOOL)isFlipped {
  return YES; // Top-left origin like Windows GDI
}

- (void)drawRect:(NSRect)dirtyRect {
  [[NSColor whiteColor] setFill];
  NSRectFill(dirtyRect);

  if (!_printer)
    return;

  std::vector<PrinterElement> elements = _printer->GetElements();

  CGContextRef context = [[NSGraphicsContext currentContext] CGContext];

  extern int g_fontSize;
  extern int g_columns;

  CGFloat y = 10.0;
  CGFloat currentX = 10.0;
  CGFloat leftMargin = 10.0;
  CGFloat currentLineMaxHeight = g_fontSize + 4;
  CGFloat width = self.bounds.size.width;

  // Font setup
  NSFont *fontNormal = [NSFont fontWithName:@"Courier New" size:g_fontSize];
  if (!fontNormal)
    fontNormal = [NSFont userFixedPitchFontOfSize:g_fontSize];

  NSDictionary *attrsNormal = @{
    NSFontAttributeName : fontNormal,
    NSForegroundColorAttributeName : [NSColor blackColor]
  };

  // Reference width for justification (ESC a). When the user has set a fixed
  // column count ("Colunas"), center/right within that paper width
  // (columns * character width); otherwise fall back to the view width.
  CGFloat charWidth = [@"0" sizeWithAttributes:attrsNormal].width;
  CGFloat paperWidth = (g_columns > 0) ? g_columns * charWidth : 0;

  // Glyph height of each ESC/POS font relative to the configured base size:
  // Font A is 12x24, Font B 9x17 and Font C smaller still (ESC M n).
  auto fontHeightFactor = [](int font) -> CGFloat {
    if (font == FONT_B)
      return 17.0 / 24.0;
    if (font == FONT_C)
      return 16.0 / 24.0;
    return 1.0;
  };

  auto fontForElement = [&](int font) -> NSFont * {
    if (font == FONT_A)
      return fontNormal;
    CGFloat size = g_fontSize * fontHeightFactor(font);
    NSFont *f = [NSFont fontWithName:@"Courier New" size:size];
    return f ? f : [NSFont userFixedPitchFontOfSize:size];
  };

  // ESC/POS horizontal coordinates are in dots; text is laid out on a
  // character grid, so dots map through the Font A cell width.
  auto dotsToPoints = [&](int dots) -> CGFloat {
    return dots * charWidth / (CGFloat)DOTS_PER_CHAR;
  };

  auto attrsForElement = [&](const PrinterElement &e) -> NSMutableDictionary * {
    NSMutableDictionary *a = [NSMutableDictionary dictionary];
    NSFont *f = fontForElement(e.font);
    if (e.isBold) {
      // ESC G double-strike shows up as bold.
      NSFont *boldFont = [[NSFontManager sharedFontManager] convertFont:f
                                                           toHaveTrait:NSBoldFontMask];
      if (boldFont)
        f = boldFont;
    }
    a[NSFontAttributeName] = f;
    // GS B knocks the glyphs out in white over an inked cell.
    a[NSForegroundColorAttributeName] =
        e.isReverse ? [NSColor whiteColor]
                    : (e.isRed ? [NSColor redColor] : [NSColor blackColor]);
    if (e.isUnderline)
      a[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);
    if (e.charSpacing > 0)
      a[NSKernAttributeName] = @(dotsToPoints(e.charSpacing)); // ESC SP
    return a;
  };

  auto stringForElement = [](const PrinterElement &e) -> NSString * {
    return [[NSString alloc] initWithBytes:e.text.data()
                                    length:e.text.size() * sizeof(wchar_t)
                                  encoding:NSUTF32LittleEndianStringEncoding];
  };

  // Total drawn width of the run of TEXT elements starting at startIdx, up to
  // the next line break (NEWLINE / CUT / BITMAP) or the end of the list.
  auto measureLineWidth = [&](size_t startIdx) -> CGFloat {
    CGFloat total = 0;
    for (size_t i = startIdx; i < elements.size(); ++i) {
      const auto &e = elements[i];
      if (e.type != ELEMENT_TEXT)
        break;
      NSString *t = stringForElement(e);
      NSSize sz = [t sizeWithAttributes:attrsForElement(e)];
      if (e.isRotated90)
        total += sz.height * e.heightScale * (CGFloat)t.length;
      else
        total += sz.width * e.widthScale;
    }
    return total;
  };

  // Tallest element in the same run, used to rotate upside-down lines.
  auto measureLineHeight = [&](size_t startIdx) -> CGFloat {
    CGFloat maxHeight = 0;
    for (size_t i = startIdx; i < elements.size(); ++i) {
      const auto &e = elements[i];
      if (e.type != ELEMENT_TEXT)
        break;
      NSString *t = stringForElement(e);
      CGFloat h = [t sizeWithAttributes:attrsForElement(e)].height * e.heightScale;
      if (h > maxHeight)
        maxHeight = h;
    }
    return maxHeight;
  };

  auto lineHasUpsideDown = [&](size_t startIdx) -> bool {
    for (size_t i = startIdx; i < elements.size(); ++i) {
      const auto &e = elements[i];
      if (e.type != ELEMENT_TEXT)
        break;
      if (e.isUpsideDown)
        return true;
    }
    return false;
  };

  // Draws one text segment at (x, y0) and reports the space it took up.
  auto drawSegment = [&](const PrinterElement &e, CGFloat x,
                         CGFloat y0) -> NSSize {
    NSMutableDictionary *attrs = attrsForElement(e);
    NSString *text = stringForElement(e);
    NSSize base = [text sizeWithAttributes:attrs];
    NSSize drawn =
        NSMakeSize(base.width * e.widthScale, base.height * e.heightScale);

    // ESC V turns each glyph 90 degrees clockwise while the line still runs
    // left to right, so the run occupies one glyph height per character.
    if (e.isRotated90) {
      CGFloat cell = base.height * e.heightScale;
      drawn = NSMakeSize(cell * (CGFloat)text.length, cell);
    }

    if (e.isReverse) {
      NSColor *ink = e.isRed ? [NSColor redColor] : [NSColor blackColor];
      [ink setFill];
      NSRectFill(NSMakeRect(x, y0, drawn.width, drawn.height));
    }

    if (e.isRotated90) {
      CGFloat cell = base.height * e.heightScale;
      CGFloat penX = x;
      for (NSUInteger i = 0; i < text.length; ++i) {
        NSString *glyph = [text substringWithRange:NSMakeRange(i, 1)];
        CGContextSaveGState(context);
        // Rotate about the glyph's own cell: in this flipped view a positive
        // angle turns clockwise on screen.
        CGContextTranslateCTM(context, penX + cell, y0);
        CGContextRotateCTM(context, M_PI_2);
        CGContextScaleCTM(context, e.widthScale, e.heightScale);
        [glyph drawAtPoint:NSMakePoint(0, 0) withAttributes:attrs];
        CGContextRestoreGState(context);
        penX += cell;
      }
      return drawn;
    }

    CGContextSaveGState(context);
    CGContextTranslateCTM(context, x, y0);
    CGContextScaleCTM(context, e.widthScale, e.heightScale);
    [text drawAtPoint:NSMakePoint(0, 0) withAttributes:attrs];
    CGContextRestoreGState(context);

    return drawn;
  };

  // Left edge of the printable area for an element, honouring GS L.
  auto elementBaseX = [&](const PrinterElement &e) -> CGFloat {
    return leftMargin + dotsToPoints(e.marginLeft);
  };

  // Width of the printable area: GS W if set, else the configured paper width,
  // else whatever the view gives us.
  auto elementAreaWidth = [&](const PrinterElement &e) -> CGFloat {
    if (e.areaWidth > 0)
      return dotsToPoints(e.areaWidth);
    if (paperWidth > 0)
      return paperWidth;
    return width - elementBaseX(e) - leftMargin;
  };

  auto alignStartX = [&](CGFloat contentWidth,
                         const PrinterElement &e) -> CGFloat {
    CGFloat base = elementBaseX(e);
    CGFloat areaW = elementAreaWidth(e);
    CGFloat startX = base;
    if (e.align == 1)
      startX = base + (areaW - contentWidth) / 2.0;
    else if (e.align == 2)
      startX = base + areaW - contentWidth;
    if (startX < base)
      startX = base;
    return startX;
  };

  // --- Page mode (ESC L) ----------------------------------------------------
  // Elements between ELEMENT_PAGE_BEGIN and ELEMENT_PAGE_END do not flow: each
  // one carries the position it was printed at inside the print area, in the
  // coordinate system of the print direction set by ESC T. Mapping page
  // coordinates onto the paper is one affine transform per direction, which
  // also produces the glyph rotation the printer applies to directions 1 to 3.
  // This view is flipped (y grows downwards), so the transforms are the same
  // ones the Windows renderer uses.
  auto pageTransform = [](int dir, CGFloat px, CGFloat py, CGFloat pw,
                          CGFloat ph) -> CGAffineTransform {
    switch (dir) {
    case 1: // bottom to top, starting at the lower-left corner
      return CGAffineTransformMake(0, -1, 1, 0, px, py + ph);
    case 2: // right to left, starting at the lower-right corner
      return CGAffineTransformMake(-1, 0, 0, -1, px + pw, py + ph);
    case 3: // top to bottom, starting at the upper-right corner
      return CGAffineTransformMake(0, 1, -1, 0, px + pw, py);
    default: // 0: left to right, starting at the upper-left corner
      return CGAffineTransformMake(1, 0, 0, 1, px, py);
    }
  };

  // Drawn size of a text or bitmap element. Bitmaps are scaled like every
  // other page coordinate so a dot keeps the same size all over the page.
  auto measureElementBox = [&](const PrinterElement &e) -> NSSize {
    if (e.type == ELEMENT_BITMAP)
      return NSMakeSize(dotsToPoints(e.width), dotsToPoints(e.height));
    NSString *t = stringForElement(e);
    NSSize sz = [t sizeWithAttributes:attrsForElement(e)];
    if (e.isRotated90) {
      CGFloat cell = sz.height * e.heightScale;
      return NSMakeSize(cell * (CGFloat)t.length, cell);
    }
    return NSMakeSize(sz.width * e.widthScale, sz.height * e.heightScale);
  };

  // How much paper the page needs when ESC W did not state a height.
  // Directions 1 and 3 print along the paper's vertical axis, so there it is
  // the text flow that decides how far down the page reaches.
  auto measurePageContentHeight = [&](size_t startIdx) -> CGFloat {
    CGFloat needed = 0;
    for (size_t i = startIdx; i < elements.size(); ++i) {
      const auto &e = elements[i];
      if (e.type == ELEMENT_PAGE_END)
        break;
      if (e.type != ELEMENT_TEXT && e.type != ELEMENT_BITMAP)
        continue;
      NSSize box = measureElementBox(e);
      CGFloat extent = (e.pageDir == 1 || e.pageDir == 3)
                           ? dotsToPoints(e.pageX) + box.width
                           : dotsToPoints(e.pageY) + box.height;
      if (extent > needed)
        needed = extent;
    }
    return needed;
  };

  // Row-major 1bpp raster of a bitmap element; GS * data arrives column-major.
  auto elementRaster =
      [](const PrinterElement &e) -> std::vector<unsigned char> {
    if (!e.isColumnFormat)
      return e.bitmapData;

    int xBytes = e.width / 8;
    int yBytes = e.height / 8;
    int widthDots = xBytes * 8;
    int heightDots = yBytes * 8;
    int stride = xBytes;
    std::vector<unsigned char> dst(stride * heightDots, 0);

    for (int col = 0; col < widthDots; col++) {
      for (int vB = 0; vB < yBytes; vB++) {
        size_t srcIdx = (size_t)col * yBytes + vB;
        if (srcIdx >= e.bitmapData.size())
          break;
        unsigned char b = e.bitmapData[srcIdx];
        for (int bit = 0; bit < 8; bit++) {
          if (!((b >> (7 - bit)) & 1))
            continue;
          int row = vB * 8 + bit;
          dst[row * stride + (col / 8)] |= (1 << (7 - (col % 8)));
        }
      }
    }
    return dst;
  };

  // 1bpp image for a bitmap element. The decode array swaps the gray ramp so
  // that a set bit comes out black.
  auto makeElementImage = [&](const PrinterElement &e) -> CGImageRef {
    std::vector<unsigned char> raster = elementRaster(e);
    if (raster.empty() || e.width <= 0 || e.height <= 0)
      return NULL;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
    CFDataRef dataRef = CFDataCreate(NULL, raster.data(), raster.size());
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(dataRef);
    CGFloat decode[] = {1.0, 0.0};
    size_t bytesPerRow = (e.width + 7) / 8;

    CGImageRef image = CGImageCreate(e.width, e.height,
                                     1, // bitsPerComponent
                                     1, // bitsPerPixel
                                     bytesPerRow, colorSpace,
                                     kCGBitmapByteOrderDefault, provider, decode,
                                     false, kCGRenderingIntentDefault);

    CGDataProviderRelease(provider);
    CFRelease(dataRef);
    CGColorSpaceRelease(colorSpace);
    return image; // the image holds its own reference to the data
  };

  // Draws a bitmap element with its top-left corner at (x, y0), scaled to
  // w x h points. The view is flipped, so the image is drawn through a local
  // flip to keep row 0 at the top.
  auto drawBitmapAt = [&](const PrinterElement &e, CGFloat x, CGFloat y0,
                          CGFloat w, CGFloat h) {
    CGImageRef image = makeElementImage(e);
    if (!image)
      return;
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0, y0 + h);
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextDrawImage(context, CGRectMake(x, 0, w, h), image);
    CGContextRestoreGState(context);
    CGImageRelease(image);
  };

  bool inPage = false;
  CGFloat pageLeft = 0, pageTop = 0, pageW = 0, pageH = 0;

  bool atLineStart = true;
  for (size_t idx = 0; idx < elements.size(); ++idx) {
    const auto &el = elements[idx];

    if (el.type == ELEMENT_PAGE_BEGIN) {
      if (!atLineStart) {
        y += currentLineMaxHeight + 4;
        currentLineMaxHeight = g_fontSize + 4;
      }
      currentX = leftMargin;
      atLineStart = true;

      // One scale on both axes, so a page turned by ESC T keeps its shape.
      pageLeft = leftMargin + dotsToPoints(el.pageX);
      pageTop = y + dotsToPoints(el.pageY);
      pageW = dotsToPoints(el.width);
      pageH = dotsToPoints(el.height);
      if (pageW <= 0)
        pageW = (paperWidth > 0) ? paperWidth : width - pageLeft - leftMargin;
      if (pageH <= 0)
        pageH = measurePageContentHeight(idx + 1); // ESC W never ran
      if (pageW < 1)
        pageW = 1;
      if (pageH < 1)
        pageH = 1;

      CGContextSaveGState(context);
      CGContextClipToRect(context, CGRectMake(pageLeft, pageTop, pageW, pageH));
      inPage = true;
      continue;
    }

    if (el.type == ELEMENT_PAGE_END) {
      if (inPage)
        CGContextRestoreGState(context);
      // The printer feeds the whole print area, filled or not, so the next
      // line starts below the page.
      y = pageTop + pageH;
      inPage = false;
      currentX = leftMargin;
      atLineStart = true;
      currentLineMaxHeight = g_fontSize + 4;
      continue;
    }

    if (inPage) {
      if (el.type != ELEMENT_TEXT && el.type != ELEMENT_BITMAP)
        continue;

      CGContextSaveGState(context);
      CGContextConcatCTM(context,
                         pageTransform(el.pageDir, pageLeft, pageTop, pageW, pageH));

      CGFloat localX = dotsToPoints(el.pageX);
      CGFloat localY = dotsToPoints(el.pageY);
      if (el.type == ELEMENT_TEXT) {
        drawSegment(el, localX, localY);
      } else {
        drawBitmapAt(el, localX, localY, dotsToPoints(el.width),
                     dotsToPoints(el.height));
      }

      CGContextRestoreGState(context);
      continue;
    }

    if (el.type == ELEMENT_TEXT) {
      // At the first text segment of a line, offset the start position for
      // center/right justification based on the whole line width.
      if (atLineStart) {
        CGFloat lineWidth = measureLineWidth(idx);
        currentX = alignStartX(lineWidth, el);
        atLineStart = false;

        // ESC {: the printer turns the whole line 180 degrees. Flipping both
        // axes for the whole line (rather than per segment) is what puts the
        // segments back in the order a real printer produces.
        CGFloat lineHeight = measureLineHeight(idx);
        if (lineHasUpsideDown(idx) && lineWidth > 0 && lineHeight > 0) {
          CGContextSaveGState(context);
          CGContextTranslateCTM(context, currentX + lineWidth, y + lineHeight);
          CGContextScaleCTM(context, -1.0, -1.0);

          CGFloat offsetX = 0;
          size_t j = idx;
          for (; j < elements.size() && elements[j].type == ELEMENT_TEXT; ++j) {
            offsetX += drawSegment(elements[j], offsetX, 0).width;
          }

          CGContextRestoreGState(context);

          if (lineHeight > currentLineMaxHeight) {
            currentLineMaxHeight = lineHeight;
          }
          currentX += lineWidth;
          idx = j - 1; // the loop's ++idx steps past the whole run
          continue;
        }
      }

      NSSize drawn = drawSegment(el, currentX, y);
      if (drawn.height > currentLineMaxHeight) {
        currentLineMaxHeight = drawn.height;
      }
      currentX += drawn.width;
    } else if (el.type == ELEMENT_SETPOS) {
      // ESC $ / ESC \: an explicit position replaces the justified start.
      CGFloat base = elementBaseX(el);
      currentX = el.absolutePos ? base + dotsToPoints(el.width)
                                : currentX + dotsToPoints(el.width);
      if (currentX < base)
        currentX = base;
      atLineStart = false;
    } else if (el.type == ELEMENT_FEED) {
      // ESC J / ESC K: vertical dots map 1:1 to points, matching how ESC 3
      // line spacing is already handled.
      y += el.height;
      currentX = elementBaseX(el);
      atLineStart = true;
      currentLineMaxHeight = g_fontSize + 4;
    } else if (el.type == ELEMENT_NEWLINE) {
      currentX = leftMargin;
      atLineStart = true;
      if (el.height > 0) {
        y += el.height;
      } else {
        y += currentLineMaxHeight + 4;
      }
      currentLineMaxHeight = g_fontSize + 4;
    } else if (el.type == ELEMENT_CUT) {
      if (currentX != leftMargin) {
        currentX = leftMargin;
        y += currentLineMaxHeight + 4;
        currentLineMaxHeight = g_fontSize + 4;
      }
      y += 10;

      // Draw dashed line
      NSBezierPath *path = [NSBezierPath bezierPath];
      [path moveToPoint:NSMakePoint(0, y)];
      [path lineToPoint:NSMakePoint(width, y)];
      CGFloat dashes[] = {5.0, 5.0};
      [path setLineDash:dashes count:2 phase:0];
      [[NSColor grayColor] setStroke];
      [path stroke];

      [@"[CUT]" drawAtPoint:NSMakePoint(width - 60, y - 8)
             withAttributes:attrsNormal];

      y += 30;
      atLineStart = true;
    } else if (el.type == ELEMENT_BITMAP) {
      if (currentX != leftMargin) {
        currentX = leftMargin;
        y += currentLineMaxHeight + 4;
        currentLineMaxHeight = g_fontSize + 4;
      }
      atLineStart = true;

      // Apply justification (ESC a): center/right within the paper width.
      // A dot maps 1:1 to a point here, as it does for ESC J feeds.
      CGFloat drawX = alignStartX(el.width, el);
      drawBitmapAt(el, drawX, y, el.width, el.height);

      y += el.height + 5;
    }
  }

  // Update total height for scrolling
  _totalHeight = y + 50;
  if (_totalHeight > self.frame.size.height) {
    if (_totalHeight != self.frame.size.height) {
      // Resize view to fit content (inside scrollview)
      // We need to do this carefully to avoid infinite loop in layout.
      // Normally done outside drawRect.
      dispatch_async(dispatch_get_main_queue(), ^{
        NSRect f = self.frame;
        if (f.size.height != self->_totalHeight) {
          f.size.height = self->_totalHeight;
          [self setFrame:f];
        }
      });
    }
  }
}

@end
