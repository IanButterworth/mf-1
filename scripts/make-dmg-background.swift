// Draws the DMG window background: a hint arrow from the app to Applications.
import AppKit

let w = 600, h = 360
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: w * 2, pixelsHigh: h * 2, bitsPerSample: 8,
                           samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                           bytesPerRow: 0, bitsPerPixel: 0)!
rep.size = NSSize(width: w, height: h)
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
NSGradient(starting: NSColor(white: 0.97, alpha: 1), ending: NSColor(white: 0.90, alpha: 1))!
    .draw(in: NSRect(x: 0, y: 0, width: w, height: h), angle: -90)
// Arrow between the two icons, which sit at x 150 and 450, y 170 from the top.
let y = CGFloat(h - 170)
let arrow = NSBezierPath()
arrow.move(to: NSPoint(x: 235, y: y))
arrow.line(to: NSPoint(x: 355, y: y))
arrow.lineWidth = 6
arrow.lineCapStyle = .round
NSColor(white: 0.55, alpha: 1).setStroke()
arrow.stroke()
let head = NSBezierPath()
head.move(to: NSPoint(x: 370, y: y))
head.line(to: NSPoint(x: 345, y: y + 16))
head.line(to: NSPoint(x: 345, y: y - 16))
head.close()
NSColor(white: 0.55, alpha: 1).setFill()
head.fill()
let text = NSAttributedString(string: "Drag MF-1 to Applications to install", attributes: [
    .font: NSFont.systemFont(ofSize: 15, weight: .medium), .foregroundColor: NSColor(white: 0.35, alpha: 1)])
text.draw(at: NSPoint(x: (CGFloat(w) - text.size().width) / 2, y: CGFloat(h) - 75))
NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
