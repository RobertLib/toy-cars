//
//  Materials.swift
//  ToyCars
//
//  The surface treatments the whole interface is assembled from. Each one is a
//  view or modifier that can be dropped on any shape, so a panel, a button and
//  a badge are all lit by the same light and never drift out of step.
//
//  Why these and not just `.background(Color)`:
//
//  * `Bevel`      - a rim that is bright on top and dark underneath. This one
//                   detail is what separates a moulded part from a rectangle.
//  * `Gloss`      - the specular crown on injection-moulded plastic. Confined
//                   to the top ~45 % of the shape, as a real highlight is.
//  * `Sheen`      - a slow diagonal light sweep. Used sparingly (logo, medals,
//                   primary buttons) as the "this is interactive" cue.
//  * `Grain`      - a static dither. Large flat gradients on OLED panels band
//                   visibly; a touch of noise hides it and reads as print.
//  * `Vignette`   - darkened corners. Focuses the eye and stops bright
//                   backdrops from fighting the panels on top of them.
//
//  On top of those sit three composed surfaces - `PanelSurface` (the light
//  card), `GlassSurface` (the smoked panel used over live gameplay) and
//  `StrokeText` (outlined display type) - plus the racing motifs:
//  `CheckerStrip`, `HazardStripes` and `SpeedLines`.
//

import SwiftUI

// ---------------------------------------------------------------- bevel + gloss

/// A two-tone rim: light along the top edge, dark along the bottom.
/// `strength` scales both; `inset` pulls it inside the shape.
struct Bevel<S: InsettableShape>: View {
    var shape: S
    var strength: Double = 1
    var width: CGFloat = 1.5

    var body: some View {
        ZStack {
            shape
                .strokeBorder(
                    LinearGradient(colors: [.white.opacity(0.75 * strength),
                                            .white.opacity(0.10 * strength),
                                            .clear],
                                   startPoint: .top, endPoint: .center),
                    lineWidth: width)
            shape
                .strokeBorder(
                    LinearGradient(colors: [.clear,
                                            TC.ink.opacity(0.10 * strength),
                                            TC.ink.opacity(0.35 * strength)],
                                   startPoint: .center, endPoint: .bottom),
                    lineWidth: width)
        }
        .allowsHitTesting(false)
    }
}

/// The specular crown of a moulded plastic part.
struct Gloss<S: Shape>: View {
    var shape: S
    var strength: Double = 0.5
    /// How far down the shape the highlight reaches.
    var extent: CGFloat = 0.46

    var body: some View {
        GeometryReader { geo in
            shape
                .fill(LinearGradient(
                    stops: [.init(color: .white.opacity(strength), location: 0),
                            .init(color: .white.opacity(strength * 0.35),
                                  location: 0.55),
                            .init(color: .clear, location: 1)],
                    startPoint: .top, endPoint: .bottom))
                .frame(height: geo.size.height * extent)
                .clipShape(shape)
        }
        .allowsHitTesting(false)
        .blendMode(.plusLighter)
    }
}

/// A diagonal light sweep that crosses the shape every `period` seconds.
/// The delay between passes is what keeps it from being noise.
struct Sheen<S: Shape>: View {
    var shape: S
    var period: Double = 4.6
    var strength: Double = 0.5
    var width: CGFloat = 0.20
    /// A `TimelineView(.animation)` redraws every display frame for as long
    /// as it is on screen, and this one carries no information at all - it is
    /// there to say "press this". So Reduce Motion gets the shape without it,
    /// rather than a slower version of it. It sits on the primary button of
    /// every screen, the title screen's name plate and the RESUME button of
    /// the pause panel, which means it was also the one thing still
    /// repainting at 60 Hz behind a paused race.
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        if reduceMotion { EmptyView() } else { animated }
    }

    private var animated: some View {
        TimelineView(.animation) { ctx in
            let t = ctx.date.timeIntervalSinceReferenceDate
                .truncatingRemainder(dividingBy: period) / period
            // Rest for the second half of the cycle, then sweep across.
            let p = min(1, max(0, t * 2))
            GeometryReader { geo in
                let w = geo.size.width
                shape
                    .fill(LinearGradient(
                        stops: [.init(color: .clear, location: 0),
                                .init(color: .white.opacity(strength),
                                      location: 0.5),
                                .init(color: .clear, location: 1)],
                        startPoint: .topLeading, endPoint: .bottomTrailing))
                    .frame(width: w * width)
                    .offset(x: -w * width + p * (w + w * width))
                    .rotationEffect(.degrees(14))
            }
            .clipShape(shape)
        }
        .allowsHitTesting(false)
        .blendMode(.plusLighter)
    }
}

// ---------------------------------------------------------------- texture

/// A fixed dither pattern. Generated once into an image and tiled, so the cost
/// is a single texture upload rather than per-frame drawing.
struct Grain: View {
    var opacity: Double = 0.05

    private static let tile: Image = {
        let n = 96
        let r = UIGraphicsImageRenderer(size: CGSize(width: n, height: n))
        let img = r.image { ctx in
            var seed: UInt64 = 0x2545F4914F6CDD1D
            func rnd() -> Double {
                seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17
                return Double(seed % 1000) / 1000
            }
            for y in 0..<n {
                for x in 0..<n {
                    let v = rnd()
                    ctx.cgContext.setFillColor(gray: v, alpha: 1)
                    ctx.cgContext.fill(CGRect(x: x, y: y, width: 1, height: 1))
                }
            }
        }
        return Image(uiImage: img)
    }()

    var body: some View {
        Grain.tile
            .resizable(resizingMode: .tile)
            .opacity(opacity)
            .blendMode(.overlay)
            .allowsHitTesting(false)
    }
}

/// Darkened corners. `power` moves the falloff in or out.
struct Vignette: View {
    var strength: Double = 0.38
    var power: CGFloat = 0.55

    var body: some View {
        GeometryReader { geo in
            let d = max(geo.size.width, geo.size.height)
            RadialGradient(
                stops: [.init(color: .clear, location: power),
                        .init(color: TC.ink.opacity(strength * 0.45),
                              location: 0.82),
                        .init(color: TC.ink.opacity(strength), location: 1)],
                center: .center, startRadius: 0, endRadius: d * 0.78)
        }
        .allowsHitTesting(false)
    }
}

// ---------------------------------------------------------------- surfaces

/// The standard light panel: cream card, bevelled rim, contact and ambient
/// shadows, a whisper of grain so it reads as printed board.
struct PanelSurface: View {
    var fill: Color = TC.paper
    var corner: CGFloat = TC.rCard
    var glossy: Bool = true

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: corner, style: .continuous)
        ZStack {
            shape
                .fill(LinearGradient(colors: [fill, fill.darker(0.05)],
                                     startPoint: .top, endPoint: .bottom))
            if glossy { Gloss(shape: shape, strength: 0.30, extent: 0.34) }
            Grain(opacity: 0.035).clipShape(shape)
            Bevel(shape: shape, strength: 0.55, width: 1.4)
        }
        .compositingGroup()
        .shadow(color: TC.contact, radius: 3, y: 2)
        .shadow(color: TC.ambient, radius: 16, y: 10)
    }
}

/// The dark translucent panel used over live gameplay and over photos: smoked
/// plastic with a bright top rim. Deliberately blur-free so it costs nothing
/// to redraw every frame in the HUD.
struct GlassSurface: View {
    var corner: CGFloat = 16
    var tint: Color = TC.ink
    var opacity: Double = 0.46

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: corner, style: .continuous)
        ZStack {
            shape.fill(
                LinearGradient(colors: [tint.opacity(opacity + 0.10),
                                        tint.opacity(opacity - 0.06)],
                               startPoint: .top, endPoint: .bottom))
            shape.strokeBorder(
                LinearGradient(colors: [.white.opacity(0.42),
                                        .white.opacity(0.06)],
                               startPoint: .top, endPoint: .bottom),
                lineWidth: 1)
        }
    }
}

// ---------------------------------------------------------------- outlined type

/// Display text with a hard keyline and an offset drop shadow - the arcade
/// racer signature. SwiftUI cannot stroke text, so the outline is drawn as
/// copies of the glyphs radiating out from centre; 16 steps is enough that the
/// join stays smooth at title sizes.
struct StrokeText: View {
    var text: Text
    var width: CGFloat = 3
    var color: Color = TC.ink
    /// A second, wider outline in a different colour (a sticker edge).
    var outer: Color? = nil
    var outerWidth: CGFloat = 2.5
    var dropShadow: CGFloat = 3
    var shadowColor: Color = TC.ink.opacity(0.42)

    private static let steps = 16

    private func ring(_ r: CGFloat, _ c: Color) -> some View {
        ZStack {
            ForEach(0..<StrokeText.steps, id: \.self) { i in
                let a = Double(i) / Double(StrokeText.steps) * 2 * .pi
                text
                    .foregroundStyle(c)
                    .offset(x: cos(a) * r, y: sin(a) * r)
            }
        }
    }

    var body: some View {
        ZStack {
            if dropShadow > 0 {
                text.foregroundStyle(shadowColor)
                    .offset(y: dropShadow + width)
            }
            if let outer {
                ring(width + outerWidth, outer)
            }
            ring(width, color)
            text
        }
    }
}

extension View {
    /// A hard offset shadow, like ink printed slightly out of register.
    func hardShadow(_ dy: CGFloat = 3,
                    _ color: Color = TC.ink.opacity(0.35)) -> some View {
        shadow(color: color, radius: 0, y: dy)
    }

    /// Grounds a floating element with the two-shadow pair used everywhere.
    func lift(_ height: CGFloat = 8) -> some View {
        self.shadow(color: TC.contact, radius: height * 0.28,
                    y: height * 0.22)
            .shadow(color: TC.ambient, radius: height * 1.6, y: height)
    }
}

// ---------------------------------------------------------------- motifs

/// A checkered band - the racing motif, used on the logo, banners and gates.
struct CheckerStrip: View {
    var squares: Int = 2
    var cell: CGFloat = 9
    var light: Color = .white
    var dark: Color = TC.ink

    var body: some View {
        Canvas { ctx, size in
            let rows = max(1, Int(size.height / cell))
            let cols = Int(ceil(size.width / cell))
            for r in 0..<rows {
                for c in 0..<cols {
                    let on = (r + c) % 2 == 0
                    ctx.fill(Path(CGRect(x: CGFloat(c) * cell,
                                         y: CGFloat(r) * cell,
                                         width: cell, height: cell)),
                             with: .color(on ? light : dark))
                }
            }
        }
        .frame(height: cell * CGFloat(squares))
        .allowsHitTesting(false)
    }
}

/// Diagonal hazard stripes - kerbs, locked panels, warning ribbons.
struct HazardStripes: View {
    var a: Color = TC.ink
    var b: Color = TC.sun
    var pitch: CGFloat = 16

    var body: some View {
        Canvas { ctx, size in
            ctx.fill(Path(CGRect(origin: .zero, size: size)),
                     with: .color(a))
            let n = Int((size.width + size.height) / pitch) + 2
            for i in 0..<n {
                var p = Path()
                let x = CGFloat(i) * pitch - size.height
                p.move(to: CGPoint(x: x, y: size.height))
                p.addLine(to: CGPoint(x: x + size.height, y: 0))
                p.addLine(to: CGPoint(x: x + size.height + pitch / 2, y: 0))
                p.addLine(to: CGPoint(x: x + pitch / 2, y: size.height))
                p.closeSubpath()
                ctx.fill(p, with: .color(b))
            }
        }
        .allowsHitTesting(false)
    }
}

/// Speed lines radiating from a point - the classic "going fast" overlay.
/// `intensity` 0…1 drives both count and opacity, so it can be wired straight
/// to the car's speed.
struct SpeedLines: View {
    var intensity: Double
    /// A clock, in seconds. The streaks travel outwards with it - lines that
    /// hold still read as a texture stuck on the screen rather than as speed.
    /// Fed from the race clock, so no timeline of its own is needed.
    var phase: Double = 0
    var color: Color = .white
    var center: UnitPoint = .center

    var body: some View {
        Canvas { ctx, size in
            guard intensity > 0.01 else { return }
            let c = CGPoint(x: size.width * center.x,
                            y: size.height * center.y)
            let far = max(size.width, size.height)
            var seed: UInt64 = 0x9E3779B97F4A7C15
            func rnd() -> Double {
                seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17
                return Double(seed % 10_000) / 10_000
            }
            let n = 34
            for _ in 0..<n {
                let a = rnd() * 2 * .pi
                // Each streak keeps its angle and its own speed, and wraps
                // through the same band, so the pattern never repeats
                // visibly and nothing pops in at full brightness.
                let travel = (phase * (0.9 + rnd() * 0.8) + rnd())
                    .truncatingRemainder(dividingBy: 1)
                let r0 = far * (0.20 + 0.48 * travel)
                let len = far * (0.06 + rnd() * 0.16) * intensity
                let w = 1.0 + rnd() * 1.8
                var p = Path()
                p.move(to: CGPoint(x: c.x + cos(a) * r0,
                                   y: c.y + sin(a) * r0))
                p.addLine(to: CGPoint(x: c.x + cos(a) * (r0 + len),
                                      y: c.y + sin(a) * (r0 + len)))
                ctx.stroke(p, with: .color(color.opacity(0.05 + 0.30
                                                         * intensity * rnd())),
                           style: StrokeStyle(lineWidth: w, lineCap: .round))
            }
        }
        .allowsHitTesting(false)
        .blendMode(.plusLighter)
    }
}
