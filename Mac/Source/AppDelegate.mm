#import "AppDelegate.h"
#import "PrinterView.h"

// C++ headers
#include "../Network.h"
#include "../VirtualPrinter.h"

@interface AppDelegate ()
@property(strong, nonatomic) PrinterView *printerView;
@end

// Global instance to bridge C++ callback to ObjC
VirtualPrinter printer;
NetworkServer server;
std::vector<unsigned char> g_rawBuffer;
const size_t MAX_BUFFER_SIZE = 1024 * 1024; // 1MB Limit
int g_port = 9100;
int g_columns = 0;

void RepaintCallback(void *param) {
  AppDelegate *delegate = (__bridge AppDelegate *)param;
  dispatch_async(dispatch_get_main_queue(), ^{
    [delegate.printerView setNeedsDisplay:YES];
  });
}

@implementation AppDelegate

- (void)setupMenu {
  NSMenu *menubar = [[NSMenu alloc] init];
  [NSApp setMainMenu:menubar];

  // App Menu
  NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
  [menubar addItem:appMenuItem];
  NSMenu *appMenu = [[NSMenu alloc] init];
  [appMenuItem setSubmenu:appMenu];

  [appMenu addItemWithTitle:@"Quit VirtualESCPOS"
                     action:@selector(terminate:)
              keyEquivalent:@"q"];

  // File Menu
  NSMenuItem *fileMenuItem = [[NSMenuItem alloc] init];
  [menubar addItem:fileMenuItem];
  NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
  [fileMenuItem setSubmenu:fileMenu];

  [fileMenu addItemWithTitle:@"Save Output..."
                      action:@selector(saveOutput:)
               keyEquivalent:@"s"];

  // Settings Menu
  NSMenuItem *settingsMenuItem = [[NSMenuItem alloc] init];
  [menubar addItem:settingsMenuItem];
  NSMenu *settingsMenu = [[NSMenu alloc] initWithTitle:@"Settings"];
  [settingsMenuItem setSubmenu:settingsMenu];

  [settingsMenu addItemWithTitle:@"Port..."
                          action:@selector(changePort:)
                   keyEquivalent:@"p"];
  [settingsMenu addItemWithTitle:@"Columns..."
                          action:@selector(changeColumns:)
                   keyEquivalent:@"c"];
}

- (void)startServerWithPort:(int)port {
  server.Stop();
  bool success = server.Start(port, [](const unsigned char *data, int len) {
    printer.ProcessData(data, len);

    // Buffer logic
    if (g_rawBuffer.size() + len > MAX_BUFFER_SIZE) {
      size_t overflow = (g_rawBuffer.size() + len) - MAX_BUFFER_SIZE;
      if (overflow < g_rawBuffer.size()) {
        g_rawBuffer.erase(g_rawBuffer.begin(), g_rawBuffer.begin() + overflow);
      } else {
        g_rawBuffer.clear();
        if ((size_t)len > MAX_BUFFER_SIZE) {
          data += (len - MAX_BUFFER_SIZE);
          len = MAX_BUFFER_SIZE;
        }
      }
    }
    g_rawBuffer.insert(g_rawBuffer.end(), data, data + len);
  });

  if (!success) {
    dispatch_async(dispatch_get_main_queue(), ^{
      NSAlert *alert = [[NSAlert alloc] init];
      [alert setMessageText:@"Error"];
      [alert
          setInformativeText:
              [NSString
                  stringWithFormat:@"Failed to start server on port %d", port]];
      [alert runModal];
    });
  } else {
    g_port = port;
    [self.window
        setTitle:[NSString
                     stringWithFormat:@"Virtual ESC/POS Printer (Port %d)",
                                      g_port]];
  }
}

- (void)loadSettings {
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];

  // Port
  if ([defaults objectForKey:@"Port"]) {
    g_port = (int)[defaults integerForKey:@"Port"];
  }

  // Columns
  if ([defaults objectForKey:@"Columns"]) {
    g_columns = (int)[defaults integerForKey:@"Columns"];
  }
}

- (void)saveSettings {
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  [defaults setInteger:g_port forKey:@"Port"];
  [defaults setInteger:g_columns forKey:@"Columns"];

  // Window Frame
  if (self.window) {
    NSString *frameString = NSStringFromRect(self.window.frame);
    [defaults setObject:frameString forKey:@"WindowFrame"];
  }

  [defaults synchronize];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
  [self loadSettings];

  // Window Setup
  // Default frame
  NSRect frame = NSMakeRect(0, 0, 500, 700);

  // Load saved frame
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  if ([defaults objectForKey:@"WindowFrame"]) {
    frame = NSRectFromString([defaults stringForKey:@"WindowFrame"]);
  } else {
    // Center if new
    // We'll center later if no saved frame, but let's just stick with default
    // rect then center
  }

  NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                     NSWindowStyleMaskResizable |
                     NSWindowStyleMaskMiniaturizable;
  self.window = [[NSWindow alloc] initWithContentRect:frame
                                            styleMask:style
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
  [self.window
      setTitle:[NSString stringWithFormat:@"Virtual ESC/POS Printer (Port %d)",
                                          g_port]];

  // View Setup
  NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:frame];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setBorderType:NSNoBorder];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  self.printerView = [[PrinterView alloc] initWithFrame:frame];
  [self.printerView setPrinter:&printer];
  printer.SetMaxColumns(g_columns); // Apply loaded columns

  [scrollView setDocumentView:self.printerView];
  [self.window setContentView:scrollView];

  if (![defaults objectForKey:@"WindowFrame"]) {
    [self.window center];
  }

  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  // Setup Printer
  printer.SetRepaintCallback(RepaintCallback, (__bridge void *)self);

  // Setup Menu
  [self setupMenu];

  // Start Server
  [self startServerWithPort:g_port];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
  [self saveSettings];
  server.Stop();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
  return YES;
}

// Actions

- (void)changePort:(id)sender {
  NSAlert *alert = [[NSAlert alloc] init];
  [alert setMessageText:@"Port Configuration"];
  [alert setInformativeText:@"Enter TCP Port:"];
  [alert addButtonWithTitle:@"OK"];
  [alert addButtonWithTitle:@"Cancel"];

  NSTextField *input =
      [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
  [input setStringValue:[NSString stringWithFormat:@"%d", g_port]];
  [alert setAccessoryView:input];

  if ([alert runModal] == NSAlertFirstButtonReturn) {
    int newPort = [input intValue];
    if (newPort > 0 && newPort != g_port) {
      [self startServerWithPort:newPort];
      [self saveSettings];
    }
  }
}

- (void)changeColumns:(id)sender {
  NSAlert *alert = [[NSAlert alloc] init];
  [alert setMessageText:@"Columns Configuration"];
  [alert setInformativeText:@"Enter Max Columns (0 = Auto):"];
  [alert addButtonWithTitle:@"OK"];
  [alert addButtonWithTitle:@"Cancel"];

  NSTextField *input =
      [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
  [input setStringValue:[NSString stringWithFormat:@"%d", g_columns]];
  [alert setAccessoryView:input];

  if ([alert runModal] == NSAlertFirstButtonReturn) {
    int newCols = [input intValue];
    if (newCols >= 0) {
      g_columns = newCols;
      printer.SetMaxColumns(g_columns);
      [self saveSettings];
    }
  }
}

- (void)saveOutput:(id)sender {
  if (g_rawBuffer.empty()) {
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Empty Buffer"];
    [alert setInformativeText:@"There is no data to save."];
    [alert runModal];
    return;
  }

  NSSavePanel *panel = [NSSavePanel savePanel];
  [panel setNameFieldStringValue:@"printer_output.txt"];

  [panel beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse result) {
                  if (result == NSModalResponseOK) {
                    NSData *data = [NSData dataWithBytes:g_rawBuffer.data()
                                                  length:g_rawBuffer.size()];
                    [data writeToURL:[panel URL] atomically:YES];
                  }
                }];
}

@end
