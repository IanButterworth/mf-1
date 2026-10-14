// Draws the app icon (a miniature of the panel) and writes AppIcon.iconset.
import AppKit

func draw(_ px: Int) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let s = CGFloat(px) / 1024
    func r(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> NSRect { NSRect(x: x * s, y: (1024 - y - h) * s, width: w * s, height: h * s) }
    // Body
    let body = NSBezierPath(roundedRect: r(100, 100, 824, 824), xRadius: 180 * s, yRadius: 180 * s)
    NSGradient(starting: NSColor(white: 0.21, alpha: 1), ending: NSColor(white: 0.13, alpha: 1))!.draw(in: body, angle: -90)
    // Screen bezel and screen
    NSColor(white: 0.04, alpha: 1).setFill()
    NSBezierPath(roundedRect: r(212, 170, 600, 380), xRadius: 50 * s, yRadius: 50 * s).fill()
    let screen = NSBezierPath(roundedRect: r(252, 206, 520, 308), xRadius: 14 * s, yRadius: 14 * s)
    NSGradient(starting: NSColor(red: 0.69, green: 0.84, blue: 0.90, alpha: 1),
               ending: NSColor(red: 0.55, green: 0.76, blue: 0.86, alpha: 1))!.draw(in: screen, angle: -90)
    let title = NSAttributedString(string: "MF-1", attributes: [
        .font: NSFont.systemFont(ofSize: 150 * s, weight: .medium),
        .foregroundColor: NSColor(red: 0.07, green: 0.12, blue: 0.16, alpha: 1)])
    let ts = title.size()
    title.draw(at: NSPoint(x: 512 * s - ts.width / 2, y: (1024 - 360) * s - ts.height / 2))
    // Keys with LED slits
    for row in 0..<2 {
        let n = row == 0 ? 5 : 6
        for i in 0..<n {
            let w: CGFloat = 92, gap: CGFloat = 24
            let total = CGFloat(n) * w + CGFloat(n - 1) * gap
            let x = 512 - total / 2 + CGFloat(i) * (w + gap)
            let y: CGFloat = row == 0 ? 600 : 730
            let key = NSBezierPath(roundedRect: r(x, y, w, 120), xRadius: 46 * s, yRadius: 46 * s)
            NSColor(white: 0.10, alpha: 1).setFill()
            key.fill()
            let lit = (row == 1 && (i == 1 || i == 4)) || (row == 0 && i == 2)
            (lit ? NSColor(red: 0.85, green: 0.96, blue: 1, alpha: 1) : NSColor(white: 0.3, alpha: 1)).setFill()
            NSBezierPath(roundedRect: r(x + w / 2 - 6, y + 30, 12, 60), xRadius: 6 * s, yRadius: 6 * s).fill()
        }
    }
    NSGraphicsContext.restoreGraphicsState()
    return rep
}

let out = CommandLine.arguments[1]
try? FileManager.default.createDirectory(atPath: out, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let name = scale == 1 ? "icon_\(base)x\(base).png" : "icon_\(base)x\(base)@2x.png"
        try! draw(base * scale).representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "\(out)/\(name)"))
    }
}
