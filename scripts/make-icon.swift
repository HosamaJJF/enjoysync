// Rebuild the PNG from the same code-native geometry as assets/icon.svg.
import AppKit
let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 512, pixelsHigh: 512,
                             bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                             isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
NSColor(red: 16/255, green: 23/255, blue: 30/255, alpha: 1).setFill()
NSBezierPath(roundedRect: NSRect(x: 0, y: 0, width: 512, height: 512), xRadius: 112, yRadius: 112).fill()
NSColor(red: 113/255, green: 231/255, blue: 189/255, alpha: 1).setFill()
for (x, height) in [(112, 96), (174, 192), (236, 288), (298, 192), (360, 96)] {
    NSBezierPath(roundedRect: NSRect(x: x, y: (512-height)/2, width: 40, height: height), xRadius: 20, yRadius: 20).fill()
}
NSGraphicsContext.restoreGraphicsState()
try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "assets/icon.png"))
