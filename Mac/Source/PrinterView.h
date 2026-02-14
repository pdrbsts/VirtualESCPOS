#import <Cocoa/Cocoa.h>

#ifdef __cplusplus
class VirtualPrinter;
#else
typedef struct VirtualPrinter VirtualPrinter;
#endif

@interface PrinterView : NSView

#ifdef __cplusplus
- (void)setPrinter:(VirtualPrinter *)printerObj;
#endif

@end
