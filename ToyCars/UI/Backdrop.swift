//
//  Backdrop.swift
//  ToyCars
//
//  The illustrated scenes behind the menus.
//
//  A flat gradient behind a menu is the single clearest tell of a prototype,
//  so every screen instead gets a small painted world: a graded sky, a sun with
//  rays, drifting cloud banks, three ridgelines of hills at different parallax
//  depths, a themed scenery band on the horizon and a play-mat road running
//  towards the viewer. All of it is drawn procedurally - no image assets - and
//  animated with a handful of `repeatForever` offsets rather than a per-frame
//  timeline, so the render server does the work and the CPU stays idle.
//
//  Depth is sold three ways at once, the way a painter would: things further
//  away move slower (parallax), lose contrast (aerial haze towards the horizon
//  colour) and lose saturation. `Ridge.haze` applies the last two together.
//

import SwiftUI

// ---------------------------------------------------------------- palette

/// Everything a scene needs. Kept as plain data so a track theme can be turned
/// into a backdrop with one lookup.
struct ScenePalette: Sendable {
    var zenith: Color
    var high: Color
    var horizon: Color
    var sun: Color
    var sunGlow: Color
    var hillFar: Color
    var hillMid: Color
    var hillNear: Color
    var ground: Color
    var road: Color
    var scenery: Scenery
    var sunAt: UnitPoint = UnitPoint(x: 0.76, y: 0.24)
    var cloudTint: Color = .white
    /// Where the road's vanishing point sits across the width. Moved off
    /// centre when something - the menu's hero car - has to stand on it.
    var roadCenter: CGFloat = 0.5

    enum Scenery: Sendable { case palms, pines, broadleaf, none }

    // ------------------------------------------------------------ presets

    /// The menu: late afternoon, warm and inviting.
    static let menu = ScenePalette(
        zenith: Color(hex: "#1b3d8f"), high: Color(hex: "#3f7fd8"),
        horizon: Color(hex: "#ffd08a"), sun: Color(hex: "#fff3c4"),
        sunGlow: Color(hex: "#ffb347"),
        hillFar: Color(hex: "#7fa6d8"), hillMid: Color(hex: "#3f7a68"),
        hillNear: Color(hex: "#255c4f"),
        ground: Color(hex: "#2f6b4f"), road: Color(hex: "#3b4252"),
        scenery: .palms,
        sunAt: UnitPoint(x: 0.58, y: 0.21),
        roadCenter: 0.62)

    static let beach = ScenePalette(
        zenith: Color(hex: "#1a7fc4"), high: Color(hex: "#54c3ec"),
        horizon: Color(hex: "#d8f3ff"), sun: Color(hex: "#ffffff"),
        sunGlow: Color(hex: "#ffe9a3"),
        hillFar: Color(hex: "#9ad8ee"), hillMid: Color(hex: "#3fb2d6"),
        hillNear: Color(hex: "#f0d59a"),
        ground: Color(hex: "#e8c98a"), road: Color(hex: "#c79a5f"),
        scenery: .palms,
        sunAt: UnitPoint(x: 0.82, y: 0.20))

    static let winter = ScenePalette(
        zenith: Color(hex: "#3f6fa8"), high: Color(hex: "#8fc2e8"),
        horizon: Color(hex: "#f2f8ff"), sun: Color(hex: "#ffffff"),
        sunGlow: Color(hex: "#cfe6ff"),
        hillFar: Color(hex: "#c8dcf0"), hillMid: Color(hex: "#96b8d8"),
        hillNear: Color(hex: "#e8f2fb"),
        ground: Color(hex: "#dfeaf6"), road: Color(hex: "#ffffff"),
        scenery: .pines,
        sunAt: UnitPoint(x: 0.24, y: 0.22),
        cloudTint: Color(hex: "#eef6ff"))

    static let sunset = ScenePalette(
        zenith: Color(hex: "#2b1a6b"), high: Color(hex: "#c8477a"),
        horizon: Color(hex: "#ffc46b"), sun: Color(hex: "#fff0b0"),
        sunGlow: Color(hex: "#ff7a3d"),
        hillFar: Color(hex: "#8a5a96"), hillMid: Color(hex: "#5b3a72"),
        hillNear: Color(hex: "#33254f"),
        ground: Color(hex: "#3d2a52"), road: Color(hex: "#4a4f58"),
        // Round-canopy trees, because that is what Sunset Circuit is made
        // of: `gen_tracks.py` dresses the "classic" theme with `tree_round`,
        // bushes and hay bales over rolling hills. The band here used to be
        // a lit city skyline, so the poster on the track select screen, the
        // loading card and the results screen all promised a night circuit
        // through a city the player then never drove through.
        scenery: .broadleaf,
        sunAt: UnitPoint(x: 0.30, y: 0.34),
        cloudTint: Color(hex: "#ffd9b0"))

    /// The colours behind a results screen, tinted by the medal earned.
    static func podium(_ m: TC.Metal?) -> ScenePalette {
        var p = ScenePalette.menu
        guard let m else {
            p.zenith = Color(hex: "#232d45"); p.high = Color(hex: "#4a5670")
            p.horizon = Color(hex: "#93a0b8"); p.sunGlow = Color(hex: "#7c8aa4")
            return p
        }
        p.zenith = m.dark.darker(0.30)
        p.high = m.body.darker(0.06)
        p.horizon = m.light
        p.sun = m.light
        p.sunGlow = m.body
        p.sunAt = UnitPoint(x: 0.5, y: 0.30)
        return p
    }

    /// The theme names come from the track manifests: "classic" is the
    /// evening circuit, "beach" the cove, "winter" the pass.
    static func forTheme(_ theme: String?) -> ScenePalette {
        switch theme {
        case "beach": return .beach
        case "winter": return .winter
        case "classic", "sunset": return .sunset
        default: return .menu
        }
    }
}

// ---------------------------------------------------------------- backdrop

struct Backdrop: View {
    var palette: ScenePalette = .menu
    /// Whether the play-mat road runs towards the viewer. Off for screens
    /// where a card sits over the lower half anyway.
    var showRoad: Bool = true
    var vignette: Double = 0.42

    /// Drifting clouds, turning sun rays and a road that scrolls towards the
    /// viewer are all standing motion behind static text - so they stop when
    /// the system asks for less of it. The scene itself is unchanged; only
    /// the `repeatForever` offsets in `animate` stay where they started.
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    @State private var cloudA: CGFloat = 0
    @State private var cloudB: CGFloat = 0
    @State private var rayTurn: Double = 0
    @State private var roadPhase: CGFloat = 0
    @State private var breathe: CGFloat = 0

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let h = geo.size.height
            let horizonY = h * 0.62

            // Every layer is placed by its top edge with `band`, not stacked.
            // A ZStack centres a child that is shorter than the stack, so an
            // offset from a ZStack means "from the middle" - which silently
            // pushes a horizon band half a screen down. `band` takes the
            // distance from the top of the screen and means it.
            ZStack {
                sky()

                sunDisc(w: w, h: h)

                // Cloud banks. The far one is smaller, paler and slower.
                band(y: h * 0.10, w: w * 2, h: h * 0.24, x: -w * 0.5 + cloudA) {
                    CloudBand(count: 5, seed: 7, tint: palette.cloudTint,
                              alpha: 0.30, scale: 0.55)
                }
                band(y: h * 0.02, w: w * 2, h: h * 0.30, x: -w * 0.5 + cloudB) {
                    CloudBand(count: 4, seed: 21, tint: palette.cloudTint,
                              alpha: 0.62, scale: 1.0)
                }

                // Ridgelines, far to near. All three stand *on* the horizon
                // line and rise above it; each runs off the bottom of the
                // screen so whatever is nearer covers the one behind it.
                // Tall ranges read as distant, low banks as close.
                ridge(peak: h * 0.145, freq: 1.4, phase: 0.4,
                      color: palette.hillFar, nearness: 0.16,
                      parallax: breathe * 0.4, w: w, h: h, horizonY: horizonY)
                ridge(peak: h * 0.090, freq: 0.9, phase: 2.1,
                      color: palette.hillMid, nearness: 0.52,
                      parallax: breathe * 0.9, w: w, h: h, horizonY: horizonY)

                sceneryBand(w: w, h: h, horizonY: horizonY)

                ridge(peak: h * 0.045, freq: 0.6, phase: 4.7,
                      color: palette.hillNear, nearness: 0.94,
                      parallax: breathe * 1.6, w: w, h: h, horizonY: horizonY)

                if showRoad {
                    band(y: horizonY - h * 0.02, w: w,
                         h: h - horizonY + h * 0.02) {
                        PlayMat(palette: palette, phase: roadPhase,
                                centerX: palette.roadCenter)
                    }
                }

                // Aerial haze pooling on the horizon ties the layers together.
                band(y: horizonY - h * 0.18, w: w, h: h * 0.30) {
                    LinearGradient(
                        stops: [.init(color: .clear, location: 0),
                                .init(color: palette.horizon.opacity(0.42),
                                      location: 0.55),
                                .init(color: .clear, location: 0.9)],
                        startPoint: .top, endPoint: .bottom)
                        .blendMode(.plusLighter)
                }

                Grain(opacity: 0.055)
                Vignette(strength: vignette)
            }
            .frame(width: w, height: h)
            .clipped()
            .onAppear { if !reduceMotion { animate(w: w) } }
        }
        .ignoresSafeArea()
    }

    // ------------------------------------------------------------ layers

    /// Places a layer by the distance of its top edge from the top of the
    /// screen, optionally nudged sideways for parallax.
    private func band<V: View>(y: CGFloat, w: CGFloat, h: CGFloat,
                               x: CGFloat = 0,
                               @ViewBuilder _ content: () -> V) -> some View {
        content()
            .frame(width: w, height: h)
            .position(x: w / 2 + x, y: y + h / 2)
            .allowsHitTesting(false)
    }

    /// A ridgeline sitting on the horizon. `peak` is how far above the horizon
    /// its highest point reaches; the amplitude and baseline of the underlying
    /// shape are worked back from that, so moving the horizon or changing the
    /// height of a range never leaves a hill floating in the sky or buried in
    /// the ground.
    private func ridge(peak: CGFloat, freq: CGFloat, phase: CGFloat,
                       color: Color, nearness: Double, parallax: CGFloat,
                       w: CGFloat, h: CGFloat,
                       horizonY: CGFloat) -> some View {
        let bandH = h - horizonY + peak
        // `Ridge` sums three sines whose amplitudes add up to 1.38.
        let amp = peak / (1.38 * bandH)
        return band(y: horizonY - peak, w: w, h: bandH, x: parallax) {
            Ridge(amp: amp, freq: freq, phase: phase, base: peak / bandH)
                .fill(Ridge.haze(color, palette.horizon, nearness))
        }
    }

    /// Sky above, ground below, with the change of material landing exactly on
    /// the horizon the hills are built around (0.62 of the height).
    private func sky() -> some View {
        LinearGradient(
            stops: [.init(color: palette.zenith, location: 0.00),
                    .init(color: palette.high, location: 0.30),
                    .init(color: palette.high.lighter(0.12), location: 0.48),
                    .init(color: palette.horizon, location: 0.615),
                    .init(color: palette.ground, location: 0.625),
                    .init(color: palette.ground.darker(0.14), location: 1.00)],
            startPoint: .top, endPoint: .bottom)
    }

    private func sunDisc(w: CGFloat, h: CGFloat) -> some View {
        let d = min(w, h) * 0.20
        return ZStack {
            // God rays: an angular gradient with alternating stops, turning
            // slowly. Cheaper and softer than drawing individual wedges.
            AngularGradient(
                stops: rayStops(),
                center: .center)
                .frame(width: d * 7, height: d * 7)
                .mask(RadialGradient(
                    stops: [.init(color: .white.opacity(0.45), location: 0.06),
                            .init(color: .white.opacity(0.14), location: 0.34),
                            .init(color: .clear, location: 0.62)],
                    center: .center, startRadius: 0, endRadius: d * 3.5))
                .rotationEffect(.degrees(rayTurn))
                .blendMode(.plusLighter)

            RadialGradient(
                stops: [.init(color: palette.sun.opacity(0.95), location: 0.0),
                        .init(color: palette.sunGlow.opacity(0.55),
                              location: 0.32),
                        .init(color: palette.sunGlow.opacity(0.0),
                              location: 1.0)],
                center: .center, startRadius: 0, endRadius: d * 2.4)
                .frame(width: d * 5, height: d * 5)
                .blendMode(.plusLighter)

            Circle()
                .fill(RadialGradient(
                    colors: [palette.sun, palette.sun.opacity(0.0)],
                    center: .center, startRadius: d * 0.28,
                    endRadius: d * 0.52))
                .frame(width: d, height: d)
        }
        .position(x: w * palette.sunAt.x, y: h * palette.sunAt.y)
        .allowsHitTesting(false)
    }

    private func rayStops() -> [Gradient.Stop] {
        var out: [Gradient.Stop] = []
        let spokes = 11
        for i in 0..<spokes {
            let t = Double(i) / Double(spokes)
            out.append(.init(color: palette.sunGlow.opacity(0.34), location: t))
            out.append(.init(color: .clear,
                             location: t + 0.5 / Double(spokes)))
        }
        out.append(.init(color: palette.sunGlow.opacity(0.34), location: 1))
        return out
    }

    /// Trees standing on the horizon line - so their feet are at `horizonY`
    /// and they grow upwards out of it.
    @ViewBuilder
    private func sceneryBand(w: CGFloat, h: CGFloat,
                             horizonY: CGFloat) -> some View {
        switch palette.scenery {
        case .none:
            EmptyView()
        case .palms:
            band(y: horizonY - h * 0.22, w: w, h: h * 0.22,
                 x: breathe * 1.2) {
                SceneryRow(kind: .palms, tint: palette.hillNear.darker(0.16),
                           seed: 3)
            }
        case .pines:
            band(y: horizonY - h * 0.18, w: w, h: h * 0.18,
                 x: breathe * 1.2) {
                SceneryRow(kind: .pines, tint: palette.hillMid.darker(0.14),
                           seed: 11)
            }
        case .broadleaf:
            band(y: horizonY - h * 0.20, w: w, h: h * 0.20,
                 x: breathe * 1.2) {
                SceneryRow(kind: .broadleaf,
                           tint: palette.hillNear.darker(0.10), seed: 5)
            }
        }
    }

    private func animate(w: CGFloat) {
        withAnimation(.linear(duration: 90).repeatForever(autoreverses: false)) {
            cloudA = w
        }
        withAnimation(.linear(duration: 54).repeatForever(autoreverses: false)) {
            cloudB = w
        }
        withAnimation(.linear(duration: 120)
            .repeatForever(autoreverses: false)) {
            rayTurn = 360
        }
        withAnimation(.linear(duration: 2.2).repeatForever(autoreverses: false)) {
            roadPhase = 1
        }
        withAnimation(.easeInOut(duration: 18).repeatForever(autoreverses: true)) {
            breathe = 14
        }
    }
}

// ---------------------------------------------------------------- ridgeline

/// A hill silhouette built from three sine waves, so no two ridges repeat.
struct Ridge: Shape {
    var amp: CGFloat        // amplitude as a fraction of height
    var freq: CGFloat       // waves across the width
    var phase: CGFloat
    var base: CGFloat       // baseline as a fraction of height

    func path(in r: CGRect) -> Path {
        var p = Path()
        let steps = 48
        p.move(to: CGPoint(x: 0, y: r.height))
        for i in 0...steps {
            let t = CGFloat(i) / CGFloat(steps)
            let x = t * r.width
            let a = t * freq * 2 * .pi + phase
            let y = r.height * base
                - r.height * amp * (sin(a) * 0.6
                                    + sin(a * 2.3 + 1.1) * 0.28
                                    + sin(a * 0.6 + 2.6) * 0.5)
            p.addLine(to: CGPoint(x: x, y: y))
        }
        p.addLine(to: CGPoint(x: r.width, y: r.height))
        p.closeSubpath()
        return p
    }

    /// Aerial perspective. The further away a ridge is - `nearness` towards 0,
    /// 1 being right in front of the viewer -
    /// the more of the horizon colour is mixed into it and the less contrast it
    /// keeps from top to bottom - the two cues that read as distance.
    /// The mix is done on the colours themselves rather than by stacking a
    /// translucent wash, so the ridge stays opaque and never lets the sky
    /// show through the hills.
    @MainActor
    static func haze(_ c: Color, _ horizon: Color,
                     _ nearness: Double) -> LinearGradient {
        let far = 1 - nearness
        let top = c.lighter(0.08 * far).blended(with: horizon, by: 0.58 * far)
        let bottom = c.darker(0.10 * nearness)
            .blended(with: horizon, by: 0.34 * far)
        return LinearGradient(colors: [top, bottom],
                              startPoint: .top, endPoint: .bottom)
    }
}

// ---------------------------------------------------------------- clouds

/// A row of soft cumulus blobs. Each cloud is a stack of overlapping ellipses,
/// which is how they are drawn by hand too - flat bottom, lumpy top.
struct CloudBand: View {
    var count: Int
    var seed: UInt64
    var tint: Color
    var alpha: Double
    var scale: CGFloat

    var body: some View {
        Canvas { ctx, size in
            var s = seed &* 0x9E3779B97F4A7C15 | 1
            func rnd() -> CGFloat {
                s ^= s << 13; s ^= s >> 7; s ^= s << 17
                return CGFloat(s % 10_000) / 10_000
            }
            for i in 0..<count {
                let cx = (CGFloat(i) + rnd() * 0.6) / CGFloat(count)
                    * size.width
                let cy = size.height * (0.25 + rnd() * 0.5)
                let base = size.height * (0.30 + rnd() * 0.24) * scale
                var blob = Path()
                let lobes = 4
                for l in 0..<lobes {
                    let lx = cx + (CGFloat(l) - CGFloat(lobes - 1) / 2)
                        * base * 0.62
                    let ly = cy - (l == 1 || l == 2 ? base * 0.22 : 0)
                    let r = base * (l == 1 || l == 2 ? 0.62 : 0.44)
                    blob.addEllipse(in: CGRect(x: lx - r, y: ly - r,
                                               width: r * 2, height: r * 1.7))
                }
                // A shallow slab under the lobes gives the flat base a
                // cumulus has, without the hard edge a full rectangle leaves.
                blob.addRoundedRect(
                    in: CGRect(x: cx - base * 1.15, y: cy - base * 0.10,
                               width: base * 2.3, height: base * 0.30),
                    cornerSize: CGSize(width: base * 0.15,
                                       height: base * 0.15))
                // Body, then a brighter crown offset upwards for volume.
                ctx.fill(blob, with: .color(tint.opacity(alpha * 0.85)))
                ctx.translateBy(x: 0, y: -base * 0.14)
                ctx.fill(blob, with: .color(.white.opacity(alpha * 0.45)))
                ctx.translateBy(x: 0, y: base * 0.14)
            }
        }
        .blur(radius: 3)
        .allowsHitTesting(false)
    }
}

// ---------------------------------------------------------------- scenery

/// Palms, pines or round-canopy trees along the horizon, matching what the
/// circuit itself is dressed with. Silhouettes only - detail at this
/// distance would read as noise.
struct SceneryRow: View {
    enum Kind { case palms, pines, broadleaf }
    var kind: Kind
    var tint: Color
    var seed: UInt64

    var body: some View {
        Canvas { ctx, size in
            var s = seed &* 0x2545F4914F6CDD1D | 1
            func rnd() -> CGFloat {
                s ^= s << 13; s ^= s >> 7; s ^= s << 17
                return CGFloat(s % 10_000) / 10_000
            }
            let n = kind == .broadleaf ? 11 : 9
            for i in 0..<n {
                let x = (CGFloat(i) + 0.5 + (rnd() - 0.5) * 0.7)
                    / CGFloat(n) * size.width
                let scale = 0.55 + rnd() * 0.45
                switch kind {
                case .palms: palm(&ctx, x: x, size: size, scale: scale)
                case .pines: pine(&ctx, x: x, size: size, scale: scale)
                case .broadleaf: broadleaf(&ctx, x: x, size: size,
                                           scale: scale, rnd: rnd)
                }
            }
        }
        .allowsHitTesting(false)
    }

    private func palm(_ ctx: inout GraphicsContext, x: CGFloat,
                      size: CGSize, scale: CGFloat) {
        let h = size.height * 0.82 * scale
        let y0 = size.height
        var trunk = Path()
        trunk.move(to: CGPoint(x: x - h * 0.045, y: y0))
        trunk.addQuadCurve(to: CGPoint(x: x + h * 0.10, y: y0 - h),
                           control: CGPoint(x: x - h * 0.02, y: y0 - h * 0.55))
        trunk.addLine(to: CGPoint(x: x + h * 0.16, y: y0 - h * 0.98))
        trunk.addQuadCurve(to: CGPoint(x: x + h * 0.055, y: y0),
                           control: CGPoint(x: x + h * 0.06,
                                            y: y0 - h * 0.55))
        trunk.closeSubpath()
        ctx.fill(trunk, with: .color(tint))

        let top = CGPoint(x: x + h * 0.12, y: y0 - h)
        for k in 0..<6 {
            let a = Double(k) / 6 * 2 * .pi + 0.3
            let len = h * 0.42
            var frond = Path()
            frond.move(to: top)
            let tipX = top.x + CGFloat(cos(a)) * len
            let tipY = top.y + CGFloat(sin(a)) * len * 0.55 - h * 0.04
            frond.addQuadCurve(
                to: CGPoint(x: tipX, y: tipY),
                control: CGPoint(x: top.x + CGFloat(cos(a)) * len * 0.5,
                                 y: top.y + CGFloat(sin(a)) * len * 0.1
                                 - h * 0.20))
            frond.addQuadCurve(
                to: top,
                control: CGPoint(x: top.x + CGFloat(cos(a)) * len * 0.5,
                                 y: top.y + CGFloat(sin(a)) * len * 0.3
                                 - h * 0.02))
            ctx.fill(frond, with: .color(tint))
        }
    }

    private func pine(_ ctx: inout GraphicsContext, x: CGFloat,
                      size: CGSize, scale: CGFloat) {
        let h = size.height * 0.9 * scale
        let y0 = size.height
        let w = h * 0.34
        var p = Path()
        p.addRect(CGRect(x: x - w * 0.09, y: y0 - h * 0.18,
                         width: w * 0.18, height: h * 0.18))
        for tier in 0..<3 {
            let t = CGFloat(tier)
            let base = y0 - h * (0.14 + t * 0.26)
            let tw = w * (1 - t * 0.22)
            let th = h * 0.40
            p.move(to: CGPoint(x: x - tw / 2, y: base))
            p.addLine(to: CGPoint(x: x, y: base - th))
            p.addLine(to: CGPoint(x: x + tw / 2, y: base))
            p.closeSubpath()
        }
        ctx.fill(p, with: .color(tint))
        // A dusting of snow on the windward side.
        var snow = Path()
        for tier in 0..<3 {
            let t = CGFloat(tier)
            let base = y0 - h * (0.14 + t * 0.26)
            let tw = w * (1 - t * 0.22)
            snow.move(to: CGPoint(x: x - tw / 2, y: base))
            snow.addLine(to: CGPoint(x: x, y: base - h * 0.40))
            snow.addLine(to: CGPoint(x: x - tw * 0.16, y: base - h * 0.06))
            snow.closeSubpath()
        }
        ctx.fill(snow, with: .color(.white.opacity(0.55)))
    }

    /// A round-canopy tree: a short trunk under two or three overlapping
    /// discs. Deliberately the same shape as `tcprops.tree_round`, which is
    /// what stands beside the circuit itself - a horizon band is only worth
    /// drawing if it is the same place the player is about to drive through.
    private func broadleaf(_ ctx: inout GraphicsContext, x: CGFloat,
                           size: CGSize, scale: CGFloat,
                           rnd: () -> CGFloat) {
        let h = size.height * 0.80 * scale
        let y0 = size.height
        let r = h * 0.30

        var trunk = Path()
        trunk.addRect(CGRect(x: x - h * 0.045, y: y0 - h * 0.46,
                             width: h * 0.09, height: h * 0.46))
        ctx.fill(trunk, with: .color(tint))

        // Three discs, the lower two set off to either side, so the canopy
        // reads as foliage rather than as a lollipop.
        var crown = Path()
        crown.addEllipse(in: CGRect(x: x - r, y: y0 - h * 0.52 - r * 1.05,
                                    width: r * 2, height: r * 1.9))
        crown.addEllipse(in: CGRect(x: x - r * 1.35 + (rnd() - 0.5) * r * 0.3,
                                    y: y0 - h * 0.42 - r * 0.8,
                                    width: r * 1.5, height: r * 1.4))
        crown.addEllipse(in: CGRect(x: x - r * 0.15 + (rnd() - 0.5) * r * 0.3,
                                    y: y0 - h * 0.44 - r * 0.85,
                                    width: r * 1.5, height: r * 1.4))
        ctx.fill(crown, with: .color(tint))
    }
}

// ---------------------------------------------------------------- play mat

/// The road running away from the viewer: a perspective trapezoid with a
/// dashed centre line whose dashes travel towards the camera, plus verge
/// stripes. It is what makes a static menu feel like it is moving.
struct PlayMat: View {
    var palette: ScenePalette
    /// 0…1, wraps. Drives the dash travel.
    var phase: CGFloat
    /// The vanishing point across the width, 0…1.
    var centerX: CGFloat = 0.5

    var body: some View {
        Canvas { ctx, size in
            let w = size.width, h = size.height
            // The road runs straight away from the viewer, so both ends stay
            // on the vanishing point and only the width changes. It has to
            // stay narrower than the screen at the near end - a road wide
            // enough to reach both edges is no longer a road in a landscape,
            // it is just a grey floor.
            let vp = w * centerX
            let topW = w * 0.055
            let botW = w * 0.76

            func edge(_ t: CGFloat, _ side: CGFloat) -> CGPoint {
                let hw = (topW + (botW - topW) * t) * 0.5
                return CGPoint(x: vp + side * hw, y: t * h)
            }

            var road = Path()
            road.move(to: edge(0, -1))
            road.addLine(to: edge(0, 1))
            road.addLine(to: edge(1, 1))
            road.addLine(to: edge(1, -1))
            road.closeSubpath()

            ctx.fill(road, with: .linearGradient(
                Gradient(colors: [palette.road.lighter(0.16),
                                  palette.road]),
                startPoint: .zero, endPoint: CGPoint(x: 0, y: h)))

            // Kerbs down both edges.
            ctx.clip(to: road)
            for side in [-1.0, 1.0] {
                let s = CGFloat(side)
                var kerb = Path()
                let a = edge(0, s), b = edge(1, s)
                kerb.move(to: a)
                kerb.addLine(to: CGPoint(x: a.x + s * 2, y: a.y))
                kerb.addLine(to: CGPoint(x: b.x + s * 26, y: b.y))
                kerb.addLine(to: b)
                kerb.closeSubpath()
                ctx.fill(kerb, with: .color(.white.opacity(0.32)))
            }

            // Centre dashes. Spacing grows towards the viewer as 1/z, which is
            // what gives the perspective away as real rather than linear.
            let dashes = 9
            for i in 0..<dashes {
                let t0 = (CGFloat(i) + phase) / CGFloat(dashes)
                let z = pow(t0, 2.1)              // perspective compression
                let z1 = pow(min(1, t0 + 0.055), 2.1)
                guard z < 1 else { continue }
                let c0 = edge(z, 0), c1 = edge(z1, 0)   // centre line
                let hw0 = (topW + (botW - topW) * z) * 0.5
                let hw1 = (topW + (botW - topW) * z1) * 0.5
                let dw0 = max(0.6, hw0 * 0.06)
                let dw1 = max(0.9, hw1 * 0.06)
                var d = Path()
                d.move(to: CGPoint(x: c0.x - dw0, y: c0.y))
                d.addLine(to: CGPoint(x: c0.x + dw0, y: c0.y))
                d.addLine(to: CGPoint(x: c1.x + dw1, y: c1.y))
                d.addLine(to: CGPoint(x: c1.x - dw1, y: c1.y))
                d.closeSubpath()
                ctx.fill(d, with: .color(TC.sun.opacity(0.62 * Double(z + 0.3))))
            }
        }
        .allowsHitTesting(false)
        .mask(LinearGradient(
            stops: [.init(color: .clear, location: 0),
                    .init(color: .black.opacity(0.55), location: 0.18),
                    .init(color: .black, location: 0.7)],
            startPoint: .top, endPoint: .bottom))
    }
}

// ---------------------------------------------------------------- showroom

/// The garage is not outdoors, so it gets its own backdrop: a dark studio with
/// one overhead light, a blueprint grid on the back wall and a glossy floor
/// that catches the light in an ellipse. The car model sits in the middle of
/// that ellipse, which is what makes it look photographed rather than pasted.
struct ShowroomBackdrop: View {
    /// The selected car's paint, used to tint the rim lights so the whole room
    /// changes colour with the car.
    var accent: Color = TC.tangerine
    @State private var flicker: Double = 0
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            ZStack {
                LinearGradient(
                    stops: [.init(color: Color(hex: "#0d1220"), location: 0),
                            .init(color: Color(hex: "#1a2338"), location: 0.45),
                            .init(color: Color(hex: "#0a0e18"), location: 0.62),
                            .init(color: Color(hex: "#141b2c"), location: 1)],
                    startPoint: .top, endPoint: .bottom)

                // Blueprint grid on the back wall, fading upwards.
                StudioGrid(cell: 46)
                    .stroke(Color.white.opacity(0.06), lineWidth: 1)
                    .frame(height: h * 0.62)
                    .offset(y: -h * 0.19)
                    .mask(LinearGradient(colors: [.clear, .black],
                                         startPoint: .top,
                                         endPoint: .bottom))

                // Overhead light cone.
                Ellipse()
                    .fill(RadialGradient(
                        colors: [.white.opacity(0.22 + flicker),
                                 .white.opacity(0.06), .clear],
                        center: .center, startRadius: 0,
                        endRadius: min(w, h) * 0.55))
                    .frame(width: w * 0.78, height: h * 0.95)
                    .offset(y: -h * 0.24)
                    .blendMode(.plusLighter)

                // Two coloured rim lights, left cool and right warm-accented,
                // so the car has an edge against the dark on both sides.
                RadialGradient(colors: [accent.opacity(0.30), .clear],
                               center: .leading, startRadius: 0,
                               endRadius: w * 0.42)
                    .blendMode(.plusLighter)
                RadialGradient(colors: [TC.sky.opacity(0.22), .clear],
                               center: .trailing, startRadius: 0,
                               endRadius: w * 0.42)
                    .blendMode(.plusLighter)

                // The lit pool on the floor.
                Ellipse()
                    .fill(RadialGradient(
                        colors: [.white.opacity(0.16), .white.opacity(0.04),
                                 .clear],
                        center: .center, startRadius: 0,
                        endRadius: w * 0.30))
                    .frame(width: w * 0.66, height: h * 0.26)
                    .offset(y: h * 0.24)
                    .blendMode(.plusLighter)

                Grain(opacity: 0.07)
                Vignette(strength: 0.62, power: 0.30)
            }
        }
        .ignoresSafeArea()
        .onAppear {
            guard !reduceMotion else { return }
            withAnimation(.easeInOut(duration: 3.4)
                .repeatForever(autoreverses: true)) {
                flicker = 0.05
            }
        }
    }
}

/// A perspective grid - horizontals compressed towards the top, verticals
/// spreading from the centre. Reads as a workshop wall.
struct StudioGrid: Shape {
    var cell: CGFloat

    func path(in r: CGRect) -> Path {
        var p = Path()
        let cols = Int(r.width / cell) + 2
        for c in 0...cols {
            let x = CGFloat(c) * cell - cell
            p.move(to: CGPoint(x: x, y: 0))
            p.addLine(to: CGPoint(x: x, y: r.height))
        }
        var y: CGFloat = r.height
        var step = cell
        while y > 0 {
            p.move(to: CGPoint(x: 0, y: y))
            p.addLine(to: CGPoint(x: r.width, y: y))
            y -= step
            step *= 0.86
            if step < 6 { break }
        }
        return p
    }
}
