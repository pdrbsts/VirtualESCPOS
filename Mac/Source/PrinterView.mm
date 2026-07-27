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

  bool atLineStart = true;
  for (size_t idx = 0; idx < elements.size(); ++idx) {
    const auto &el = elements[idx];
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
      // Implementation of bitmap drawing
      // Convert data if needed
      std::vector<unsigned char> rasterData;

      if (el.isColumnFormat) {
        int xBytes = el.width / 8;
        int yBytes = el.height / 8;

        // Helper lambda for conversion
        auto convert = [&](const std::vector<unsigned char> &src, int x,
                           int y) {
          int widthDots = x * 8;
          int heightDots = y * 8;
          int stride = x;
          std::vector<unsigned char> dst(stride * heightDots, 0);

          for (int col = 0; col < widthDots; col++) {
            for (int vB = 0; vB < y; vB++) {
              int srcIdx = col * y + vB;
              if (srcIdx >= src.size())
                break;

              unsigned char b = src[srcIdx];
              for (int bit = 0; bit < 8; bit++) {
                bool isBlack = (b >> (7 - bit)) & 1;
                if (isBlack) {
                  int row = vB * 8 + bit;
                  int dstIdx = row * stride + (col / 8);
                  int dstBit = 7 - (col % 8);
                  dst[dstIdx] |= (1 << dstBit);
                }
              }
            }
          }
          return dst;
        };

        rasterData = convert(el.bitmapData, xBytes, yBytes);
      } else {
        rasterData = el.bitmapData;
      }

      // Check for empty data
      if (rasterData.empty() || el.width <= 0 || el.height <= 0) {
        y += el.height + 5;
        continue;
      }

      // Invert data because using Black=0/White=1 color space trick or just use
      // Mask? Raster data: 1 = Black, 0 = White. CGColorSpaceCreateDeviceGray:
      // 0.0 = Black, 1.0 = White. If we use 1 bit per pixel: If bit is 1, value
      // is max index -> 1 -> White? If bit is 0, value is min index -> 0 ->
      // Black?

      // Usually for 1bpp:
      // We want 1 -> Black.
      // We can use a decode array [1.0, 0.0] to swap.

      CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
      CFDataRef dataRef =
          CFDataCreate(NULL, rasterData.data(), rasterData.size());
      CGDataProviderRef provider = CGDataProviderCreateWithCFData(dataRef);

      CGFloat decode[] = {1.0, 0.0}; // Invert: 0->1(White), 1->0(Black)

      // bytesPerRow = ceil(width/8)
      size_t bytesPerRow = (el.width + 7) / 8;

      CGImageRef image =
          CGImageCreate(el.width, el.height,
                        1, // bitsPerComponent
                        1, // bitsPerPixel
                        bytesPerRow, colorSpace,
                        kCGBitmapByteOrderDefault, // CGBitmapInfo
                        provider, decode,
                        false, // interpolate
                        kCGRenderingIntentDefault);

      if (image) {
        // Draw image
        // Context is flipped?
        // PrinterView isFlipped returns YES. (0,0 is top-left).
        // CGContextDrawImage draws with origin at bottom-left of the rect
        // provided, but since the view is flipped, the coordinate system is
        // y-down. However, CGImage is also defined with bottom-up data usually?
        // If we draw it directly, it might be upside down.

        // Let's draw it and see. The conversion logic assumes top-down raster
        // (row 0 is top). If the View is flipped, drawing at (x,y,w,h) should
        // place row 0 at y. Wait, CGContextDrawImage in a flipped context
        // usually draws the image upside down relative to the context because
        // the image coordinate system is y-up.

        CGContextSaveGState(context);

        // Apply justification (ESC a): center/right within the paper width
        CGFloat drawX = alignStartX(el.width, el);

        // Flip context for image drawing
        CGContextTranslateCTM(context, 0, y + el.height);
        CGContextScaleCTM(context, 1.0, -1.0);

        // Draw at (drawX, 0, width, height) in the transformed space
        // Wait, we translated to (0, y+h).
        // So new (0,0) is at old (0, y+h).
        // To draw from y to y+h:
        // We want the bottom of the image at the bottom of the rect?

        CGRect imageRect = CGRectMake(drawX, 0, el.width, el.height);
        CGContextDrawImage(context, imageRect, image);

        CGContextRestoreGState(context);

        CGImageRelease(image);
      }

      CGDataProviderRelease(provider);
      CFRelease(dataRef);
      CGColorSpaceRelease(colorSpace);

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
