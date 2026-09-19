//
//  make-icon.swift — renders GamepadBridge's app icon.
//  Copyright (C) 2026 GamepadBridge contributors
//  SPDX-License-Identifier: GPL-2.0-or-later
//
//  Drawn in code rather than shipped as a binary asset so it stays diffable
//  and can be re-rendered at any size. Deliberately uses no Xbox or Microsoft
//  artwork: the motif is a gamepad under a suspension bridge, for the name.
//
//  swift packaging/icon/make-icon.swift <output-dir>
//

import AppKit
import CoreGraphics
import Foundation

let side = 1024.0

func squircle(_ rect: CGRect, radius: CGFloat) -> CGPath {
    CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius,
           transform: nil)
}

func draw(into ctx: CGContext) {
    let full = CGRect(x: 0, y: 0, width: side, height: side)

    // Background: rounded square with a top-to-bottom green gradient.
    ctx.saveGState()
    ctx.addPath(squircle(full.insetBy(dx: 40, dy: 40), radius: 230))
    ctx.clip()

    let space = CGColorSpaceCreateDeviceRGB()
    let gradient = CGGradient(colorsSpace: space,
                              colors: [
                                  CGColor(red: 0.243, green: 0.816, blue: 0.443, alpha: 1),
                                  CGColor(red: 0.043, green: 0.459, blue: 0.290, alpha: 1),
                              ] as CFArray,
                              locations: [0, 1])!

    ctx.drawLinearGradient(gradient,
                           start: CGPoint(x: 0, y: side),
                           end: CGPoint(x: 0, y: 0),
                           options: [])
    ctx.restoreGState()

    ctx.translateBy(x: side / 2, y: side / 2)

    // Suspension bridge: the cable hangs from the pylon tops, as it must —
    // drawn from the same Bezier the hangers are sampled off, so they always
    // meet it exactly.
    let deckY = 120.0
    let pylonX = 250.0
    let pylonTop = 370.0

    let cable = (p0: CGPoint(x: -pylonX, y: pylonTop),
                 p1: CGPoint(x: -110, y: 200),
                 p2: CGPoint(x: 110, y: 200),
                 p3: CGPoint(x: pylonX, y: pylonTop))

    func cablePoint(_ t: CGFloat) -> CGPoint {
        let u = 1 - t

        return CGPoint(
            x: u * u * u * cable.p0.x + 3 * u * u * t * cable.p1.x
                + 3 * u * t * t * cable.p2.x + t * t * t * cable.p3.x,
            y: u * u * u * cable.p0.y + 3 * u * u * t * cable.p1.y
                + 3 * u * t * t * cable.p2.y + t * t * t * cable.p3.y)
    }

    ctx.saveGState()
    ctx.setStrokeColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.95))
    ctx.setLineCap(.round)

    // Hangers first, so the heavier cable and pylons sit on top of them.
    ctx.setLineWidth(16)
    for step in 1...5 {
        let point = cablePoint(CGFloat(step) / 6.0)

        ctx.move(to: point)
        ctx.addLine(to: CGPoint(x: point.x, y: deckY))
    }
    ctx.strokePath()

    ctx.setLineWidth(28)
    ctx.move(to: cable.p0)
    ctx.addCurve(to: cable.p3, control1: cable.p1, control2: cable.p2)
    ctx.strokePath()

    ctx.setLineWidth(36)
    ctx.move(to: CGPoint(x: -pylonX, y: deckY))
    ctx.addLine(to: CGPoint(x: -pylonX, y: pylonTop))
    ctx.move(to: CGPoint(x: pylonX, y: deckY))
    ctx.addLine(to: CGPoint(x: pylonX, y: pylonTop))
    ctx.strokePath()

    ctx.setLineWidth(32)
    ctx.move(to: CGPoint(x: -340, y: deckY))
    ctx.addLine(to: CGPoint(x: 340, y: deckY))
    ctx.strokePath()
    ctx.restoreGState()

    // Gamepad, solid white with the controls punched out.
    ctx.beginTransparencyLayer(auxiliaryInfo: nil)

    let body = CGMutablePath()
    body.addEllipse(in: CGRect(x: -330, y: -300, width: 300, height: 300))
    body.addEllipse(in: CGRect(x: 30, y: -300, width: 300, height: 300))
    body.addPath(squircle(CGRect(x: -290, y: -230, width: 580, height: 260),
                          radius: 90))

    ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 1))
    ctx.addPath(body)
    ctx.fillPath()

    ctx.setBlendMode(.clear)

    // D-pad.
    let cross = CGMutablePath()
    cross.addPath(squircle(CGRect(x: -245, y: -130, width: 160, height: 48),
                           radius: 24))
    cross.addPath(squircle(CGRect(x: -189, y: -186, width: 48, height: 160),
                           radius: 24))
    ctx.addPath(cross)
    ctx.fillPath()

    // Face buttons.
    for (dx, dy) in [(0.0, 60.0), (0.0, -60.0), (-60.0, 0.0), (60.0, 0.0)] {
        ctx.addEllipse(in: CGRect(x: 165 + dx - 28, y: -106 + dy - 28,
                                  width: 56, height: 56))
    }
    ctx.fillPath()

    ctx.setBlendMode(.normal)
    ctx.endTransparencyLayer()
}

// Render each size natively rather than downscaling one big bitmap: the
// 16pt and 32pt variants keep their edges crisp that way.
func render(size: Int, to url: URL) {
    let space = CGColorSpaceCreateDeviceRGB()
    let ctx = CGContext(data: nil, width: size, height: size,
                        bitsPerComponent: 8, bytesPerRow: 0, space: space,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

    let scale = CGFloat(size) / side
    ctx.scaleBy(x: scale, y: scale)
    ctx.setAllowsAntialiasing(true)

    draw(into: ctx)

    let dest = CGImageDestinationCreateWithURL(url as CFURL,
                                               "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, ctx.makeImage()!, nil)
    CGImageDestinationFinalize(dest)
}

let outputDir = CommandLine.arguments.count > 1
    ? CommandLine.arguments[1] : "."

let iconset = URL(fileURLWithPath: "\(outputDir)/GamepadBridge.iconset")

try? FileManager.default.createDirectory(at: iconset,
                                         withIntermediateDirectories: true)

// The set iconutil expects: every base size at 1x and 2x.
for base in [16, 32, 128, 256, 512] {
    render(size: base,
           to: iconset.appendingPathComponent("icon_\(base)x\(base).png"))
    render(size: base * 2,
           to: iconset.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}

render(size: 1024,
       to: URL(fileURLWithPath: "\(outputDir)/icon_1024.png"))

print("wrote \(iconset.path) and icon_1024.png")
