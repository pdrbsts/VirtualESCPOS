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

  for (const auto &el : elements) {
    if (el.type == ELEMENT_TEXT) {
      // Font selection logic - always use normal font, scale using CTM
      NSFont *fontToUse = fontNormal;

      NSMutableDictionary *attrs =
          [NSMutableDictionary dictionaryWithDictionary:attrsNormal];
      attrs[NSFontAttributeName] = fontToUse;
      if (el.isRed) {
        attrs[NSForegroundColorAttributeName] = [NSColor redColor];
      }
      if (el.isUnderline) {
        attrs[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);
      }

      NSString *text =
          [[NSString alloc] initWithBytes:el.text.data()
                                   length:el.text.size() * sizeof(wchar_t)
                                 encoding:NSUTF32LittleEndianStringEncoding];

      NSSize originalSize = [text sizeWithAttributes:attrs];

      CGFloat scaleX = el.isDoubleWidth ? 2.0 : 1.0;
      CGFloat scaleY = el.isDoubleHeight ? 2.0 : 1.0;

      CGContextSaveGState(context);
      CGContextTranslateCTM(context, currentX, y);
      CGContextScaleCTM(context, scaleX, scaleY);

      [text drawAtPoint:NSMakePoint(0, 0) withAttributes:attrs];

      CGContextRestoreGState(context);

      // Update metrics
      CGFloat drawnWidth = originalSize.width * scaleX;
      CGFloat drawnHeight = originalSize.height * scaleY;

      if (drawnHeight > currentLineMaxHeight) {
        currentLineMaxHeight = drawnHeight;
      }
      currentX += drawnWidth;
    } else if (el.type == ELEMENT_NEWLINE) {
      currentX = leftMargin;
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
    } else if (el.type == ELEMENT_BITMAP) {
      if (currentX != leftMargin) {
        currentX = leftMargin;
        y += currentLineMaxHeight + 4;
        currentLineMaxHeight = g_fontSize + 4;
      }
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

        // Flip context for image drawing
        CGContextTranslateCTM(context, 0, y + el.height);
        CGContextScaleCTM(context, 1.0, -1.0);

        // Draw at (currentX, 0, width, height) in the transformed space
        // Wait, we translated to (0, y+h).
        // So new (0,0) is at old (0, y+h).
        // To draw from y to y+h:
        // We want the bottom of the image at the bottom of the rect?

        CGRect imageRect = CGRectMake(currentX, 0, el.width, el.height);
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
