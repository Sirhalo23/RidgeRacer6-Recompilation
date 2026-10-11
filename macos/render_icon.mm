// Render the player's extracted title artwork into a macOS icon tile.
// This source contains no game artwork.
#import <AppKit/AppKit.h>

#include <cstdio>

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 3) {
      fprintf(stderr, "usage: render_icon title.png output.png\n");
      return 2;
    }
    NSImage* artwork = [[NSImage alloc] initWithContentsOfFile:
        [NSString stringWithUTF8String:argv[1]]];
    if (!artwork || artwork.size.width <= 0 || artwork.size.height <= 0) {
      fprintf(stderr, "Cannot decode extracted title artwork.\n");
      return 1;
    }
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr pixelsWide:1024 pixelsHigh:1024
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    if (!bitmap) return 1;
    NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    context.imageInterpolation = NSImageInterpolationHigh;
    [[NSColor clearColor] setFill];
    NSRectFillUsingOperation(NSMakeRect(0, 0, 1024, 1024), NSCompositingOperationCopy);

    const NSRect tile = NSMakeRect(72, 80, 880, 880);
    NSBezierPath* shape = [NSBezierPath bezierPathWithRoundedRect:tile xRadius:184 yRadius:184];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* shadow = [NSShadow new];
    shadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.34];
    shadow.shadowBlurRadius = 26;
    shadow.shadowOffset = NSMakeSize(0, -14);
    [shadow set];
    [[NSColor blackColor] setFill];
    [shape fill];
    [NSGraphicsContext restoreGraphicsState];

    [NSGraphicsContext saveGraphicsState];
    [shape addClip];
    // Keep the full badge and publisher mark clear of the rounded corners.
    [artwork drawInRect:NSInsetRect(tile, 64, 64) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
        fraction:1 respectFlipped:NO hints:nil];
    [NSGraphicsContext restoreGraphicsState];
    [[NSColor colorWithWhite:1 alpha:0.16] setStroke];
    shape.lineWidth = 2;
    [shape stroke];
    [NSGraphicsContext restoreGraphicsState];

    NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    NSError* error = nil;
    if (!png || ![png writeToFile:[NSString stringWithUTF8String:argv[2]]
        options:NSDataWritingAtomic error:&error]) {
      fprintf(stderr, "Cannot write rendered icon: %s\n", error.description.UTF8String);
      return 1;
    }
    return 0;
  }
}
