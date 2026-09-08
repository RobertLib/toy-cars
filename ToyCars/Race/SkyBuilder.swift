//
//  SkyBuilder.swift
//  ToyCars
//
//  The sky is rendered at runtime into an equirectangular image. It doubles
//  as the scene background and as the image-based lighting source, so models
//  pick up the right colour reflections for each track theme - and the bundle
//  needs no HDR textures.
//

import CoreGraphics
import Foundation
import RealityKit
import UIKit
import simd

/// `nonisolated` is load-bearing. This project builds with
/// `SWIFT_DEFAULT_ACTOR_ISOLATION = MainActor`, so an unannotated type is
/// main-actor isolated - and the whole point of this one is that its work
/// must *not* happen there. `makeImage` is half a million pixels of
/// four-octave 3D value noise: 25 ms in an optimised build on a Mac, and
/// three to five times that on a phone. Run on the main actor - which is
/// where `RaceScene.load` and every `CarPreviewView` call it from - that is
/// a tenth of a second with the loading card's progress bar and its tip
/// frozen mid-animation. `RaceEngine` already slices a 145 ms catch-up into
/// pieces rather than show a stall that size; this held itself to a lower
/// standard.
nonisolated enum SkyBuilder {

    /// The sky, rendered off the main actor.
    ///
    /// The colours are unwrapped here, in the caller's own context, so that
    /// nothing but plain numbers crosses into the task - which is also why
    /// the renderer below takes vectors rather than `UIColor`s.
    static func makeImage(zenith: UIColor, horizon: UIColor, ground: UIColor,
                          sunDir: SIMD3<Float>, sunColor: UIColor,
                          clouds: Float, size: CGSize = CGSize(width: 1024,
                                                               height: 512))
    async -> CGImage? {
        let z = rgb(zenith), ho = rgb(horizon), g = rgb(ground)
        let sc = rgb(sunColor)
        let sun = simd_normalize(sunDir)
        let w = Int(size.width), h = Int(size.height)
        let box = await Task.detached(priority: .userInitiated) {
            SkyImage(image: render(width: w, height: h, zenith: z,
                                   horizon: ho, ground: g, sun: sun,
                                   sunColor: sc, clouds: clouds))
        }.value
        return box.image
    }

    /// A `CGImage` is a reference type CoreGraphics makes no Sendable promise
    /// about, so it is carried out of the task in a box that says plainly
    /// what the promise rests on: the image is finished and frozen before
    /// `render` returns it, and nothing else ever holds a second reference.
    private struct SkyImage: @unchecked Sendable {
        let image: CGImage?
    }

    private static func render(width w: Int, height h: Int,
                               zenith z: SIMD3<Float>,
                               horizon ho: SIMD3<Float>,
                               ground g: SIMD3<Float>,
                               sun: SIMD3<Float>,
                               sunColor sc: SIMD3<Float>,
                               clouds: Float) -> CGImage? {
        let cs = CGColorSpaceCreateDeviceRGB()
        guard let ctx = CGContext(data: nil, width: w, height: h,
                                  bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: cs,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast
                                    .rawValue) else { return nil }
        guard let buf = ctx.data else { return nil }
        let px = buf.bindMemory(to: UInt8.self, capacity: w * h * 4)

        for y in 0..<h {
            // v = 0 at the top (zenith), 1 at the bottom (ground)
            let v = (Float(y) + 0.5) / Float(h)
            let theta = v * Float.pi                 // 0..pi from the zenith
            let cosT = cos(theta)
            var base: SIMD3<Float>
            if cosT >= 0 {
                let t = powf(1 - cosT, 1.5)
                base = mix(z, ho, t)
            } else {
                let t = min(1, powf(-cosT, 0.55) * 1.35)
                base = mix(ho, g, t)
            }
            for x in 0..<w {
                let u = (Float(x) + 0.5) / Float(w)
                let phi = u * 2 * Float.pi
                let dir = SIMD3<Float>(sin(theta) * sin(phi), cosT,
                                       sin(theta) * cos(phi))
                var c = base

                // sun glow
                let d = max(0, simd_dot(dir, -sun))
                let glow = powf(d, 90) * 1.6 + powf(d, 6) * 0.30
                c += sc * glow

                // soft clouds above the horizon
                if clouds > 0 && cosT > 0.02 {
                    let n = fbm(dir.x * 3.1 + 11, dir.z * 3.1 - 4,
                                dir.y * 2.2)
                    let band = smoothstep(0.02, 0.42, cosT)
                        * (1 - smoothstep(0.55, 0.95, cosT))
                    let f = max(0, n - 0.42) * 2.2 * band * clouds
                    c = mix(c, SIMD3<Float>(1, 1, 1), min(0.85, f))
                }

                let o = (y * w + x) * 4
                px[o] = clamp8(c.x)
                px[o + 1] = clamp8(c.y)
                px[o + 2] = clamp8(c.z)
                px[o + 3] = 255
            }
        }
        return ctx.makeImage()
    }

    // ------------------------------------------------------------ helpers

    private static func rgb(_ c: UIColor) -> SIMD3<Float> {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        c.getRed(&r, green: &g, blue: &b, alpha: &a)
        return SIMD3(Float(r), Float(g), Float(b))
    }

    private static func mix(_ a: SIMD3<Float>, _ b: SIMD3<Float>,
                            _ t: Float) -> SIMD3<Float> {
        a + (b - a) * max(0, min(1, t))
    }

    private static func clamp8(_ v: Float) -> UInt8 {
        UInt8(max(0, min(255, v * 255)))
    }

    private static func hash3(_ x: Int, _ y: Int, _ z: Int) -> Float {
        var n = UInt64(bitPattern: Int64(x &* 374_761_393 &+ y &* 668_265_263
                                         &+ z &* 2_147_483_647))
        n = (n ^ (n >> 13)) &* 1_274_126_177
        return Float((n >> 33) & 0xFFFF) / 65535.0
    }

    private static func vnoise(_ x: Float, _ y: Float, _ z: Float) -> Float {
        let ix = Int(floor(x)), iy = Int(floor(y)), iz = Int(floor(z))
        let fx = x - floor(x), fy = y - floor(y), fz = z - floor(z)
        let sx = fx * fx * (3 - 2 * fx)
        let sy = fy * fy * (3 - 2 * fy)
        let sz = fz * fz * (3 - 2 * fz)
        func l(_ a: Float, _ b: Float, _ t: Float) -> Float { a + (b - a) * t }
        let c000 = hash3(ix, iy, iz), c100 = hash3(ix + 1, iy, iz)
        let c010 = hash3(ix, iy + 1, iz), c110 = hash3(ix + 1, iy + 1, iz)
        let c001 = hash3(ix, iy, iz + 1), c101 = hash3(ix + 1, iy, iz + 1)
        let c011 = hash3(ix, iy + 1, iz + 1), c111 = hash3(ix + 1, iy + 1, iz + 1)
        let x00 = l(c000, c100, sx), x10 = l(c010, c110, sx)
        let x01 = l(c001, c101, sx), x11 = l(c011, c111, sx)
        return l(l(x00, x10, sy), l(x01, x11, sy), sz)
    }

    private static func fbm(_ x: Float, _ y: Float, _ z: Float) -> Float {
        var a: Float = 0.5, f: Float = 1, s: Float = 0, n: Float = 0
        for _ in 0..<4 {
            s += vnoise(x * f, y * f, z * f) * a
            n += a
            a *= 0.5
            f *= 2.1
        }
        return s / n
    }

    private static func smoothstep(_ a: Float, _ b: Float, _ x: Float) -> Float {
        let t = max(0, min(1, (x - a) / max(1e-6, b - a)))
        return t * t * (3 - 2 * t)
    }
}

extension UIColor {
    convenience init(hex: String) {
        var s = hex.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("#") { s.removeFirst() }
        var v: UInt64 = 0
        Scanner(string: s).scanHexInt64(&v)
        self.init(red: CGFloat((v >> 16) & 0xff) / 255,
                  green: CGFloat((v >> 8) & 0xff) / 255,
                  blue: CGFloat(v & 0xff) / 255, alpha: 1)
    }
}
