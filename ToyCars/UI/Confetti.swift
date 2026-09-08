//
//  Confetti.swift
//  ToyCars
//
//  Confetti on the results screen after a win or a podium finish.
//
//  Two kinds of piece are mixed, because real confetti is not uniform: flat
//  rectangles that tumble, and long streamers that flutter. Each piece spins
//  about its own axis at its own rate and is shaded lighter on one face than
//  the other, so the sheet reads as having two sides rather than as coloured
//  dots. The whole thing is one `Canvas`, so a hundred pieces cost one draw.
//

import SwiftUI

struct ConfettiView: View {
    var count: Int = 70
    /// Seconds a piece takes to fall the height of the screen.
    var fall: Double = 1.9
    /// A screenful of tumbling paper is exactly the kind of thing Reduce
    /// Motion is switched on to stop, and the screen it lands on says
    /// everything it has to say without it.
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var settled = false
    /// When the confetti was thrown. Fixed at the first build of the view, so
    /// the fall starts where the screen does.
    @State private var start = Date()

    private struct Piece {
        var x: Double, delay: Double, speed: Double, size: Double
        var spin: Double, wobble: Double
        var streamer: Bool
        var tilt: Double
        /// The two faces of the sheet, struck once here.
        ///
        /// They used to be worked out inside the `Canvas` draw closure, so
        /// every frame put a hundred-odd `Color` values through
        /// `UIColor(_:)` and an HSB round trip - six thousand colour-space
        /// conversions a second, for two constants per piece.
        var light: Color, dark: Color
    }

    private let pieces: [Piece]
    /// When the last piece has finished its one fall. The sheet used to
    /// cycle for ever, so a `TimelineView(.animation)` - a redraw every
    /// display frame - stayed alive for as long as the player sat reading
    /// their result. Confetti is thrown once.
    private let duration: Double

    init(count: Int = 70, fall: Double = 1.9) {
        self.count = count
        self.fall = fall
        let palette: [Color] = [TC.sun, TC.cherry, TC.mint, TC.sky,
                                TC.grape, TC.lime, TC.tangerine, TC.rose]
        var g = SystemRandomNumberGenerator()
        pieces = (0..<count).map { i in
            let colour = palette.randomElement(using: &g) ?? TC.sun
            return Piece(x: Double.random(in: -0.05...1.05, using: &g),
                         delay: Double.random(in: 0...1.6, using: &g),
                         speed: Double.random(in: 0.75...1.35, using: &g),
                         size: Double.random(in: 7...15, using: &g),
                         spin: Double.random(in: -4...4, using: &g),
                         wobble: Double.random(in: 0.6...2.4, using: &g),
                         streamer: i % 5 == 0,
                         tilt: Double.random(in: -0.5...0.5, using: &g),
                         light: colour.lighter(0.12),
                         dark: colour.darker(0.22))
        }
        // The slowest piece, plus the longest delay before it is dropped.
        duration = pieces.map { $0.delay + fall / $0.speed }.max() ?? fall
    }

    var body: some View {
        if reduceMotion || settled { EmptyView() } else { animated }
    }

    private var animated: some View {
        TimelineView(.animation) { ctx in
            // Measured from the throw, not from the reference date, so a
            // piece has a single fall with a beginning and an end rather
            // than a position in an endless cycle.
            let now = ctx.date.timeIntervalSince(start)
            Canvas { g, size in
                for p in pieces {
                    let cycle = fall / p.speed
                    let local = now - p.delay
                    guard local > 0, local < cycle else { continue }
                    let t = local / cycle
                    let y = t * (size.height + 60) - 30
                    // Drift sideways as it falls, faster as it gets lower.
                    let x = p.x * size.width
                        + sin(now * p.wobble + p.x * 10) * 26
                        + p.tilt * t * 60

                    let spin = now * p.spin + p.x * 6
                    // A flat sheet seen edge-on nearly vanishes; that is what
                    // makes the tumble read as three dimensional.
                    let foreshorten = abs(cos(spin))
                    let w = p.streamer ? p.size * 0.45 : p.size
                    let hgt = (p.streamer ? p.size * 2.6 : p.size * 0.55)
                        * max(0.12, foreshorten)
                    // The back of the sheet is the darker side.
                    let shade = cos(spin) > 0 ? p.light : p.dark

                    var path = Path(roundedRect:
                        CGRect(x: -w / 2, y: -hgt / 2, width: w, height: hgt),
                                    cornerRadius: 1.5)
                    path = path.applying(
                        CGAffineTransform(rotationAngle: sin(spin * 0.7) * 0.9
                                          + p.tilt))
                    path = path.applying(
                        CGAffineTransform(translationX: x, y: y))
                    g.fill(path, with: .color(shade.opacity(0.94)))
                }
            }
        }
        .allowsHitTesting(false)
        .ignoresSafeArea()
        // Takes the timeline down once the last piece is off the bottom.
        .task {
            try? await Task.sleep(for: .seconds(duration))
            settled = true
        }
    }
}
