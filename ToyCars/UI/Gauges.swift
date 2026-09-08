//
//  Gauges.swift
//  ToyCars
//
//  The instruments. A racing HUD is read out of the corner of the eye while
//  the player is busy with a corner, so these follow the dial conventions a
//  real dashboard uses - a needle sweeping a fixed arc, tick marks to give the
//  needle a scale, a red zone at the top end - rather than inventing new
//  shapes. The number in the middle is for confirming; the needle is for
//  feeling.
//
//  These views redraw on every simulation frame, so there is deliberately no
//  blur, no shadow with a large radius and no `TimelineView` in here. Cost is
//  a handful of stroked paths.
//

import SwiftUI

// ---------------------------------------------------------------- speedometer

/// The speedometer: chrome bezel, dark face, coloured sweep that fills with
/// speed, red zone in the last fifth, tapered needle and a digital readout in
/// the hub. When the turbo is lit an outer ring charges up in cyan.
struct SpeedoGauge: View {
    var speed: Float            // km/h
    var maxSpeed: Float         // full-scale deflection
    var boost: Float            // 0…1, turbo remaining
    var size: CGFloat = 132

    /// The arc runs from lower-left to lower-right, leaving a gap at the
    /// bottom for the hub - the layout of nearly every car's tachometer.
    private let start: Double = 135
    private let sweep: Double = 270

    private var fraction: Double {
        Double(min(1, max(0, speed / max(1, maxSpeed))))
    }

    private var needleAngle: Double { start + sweep * fraction }

    private var arcColor: Color {
        // Green while there is headroom, amber approaching the limit, red at
        // the top - the same reading as a rev counter.
        if fraction > 0.86 { return TC.cherry }
        if fraction > 0.62 { return TC.sun }
        return TC.mint
    }

    var body: some View {
        ZStack {
            // Face.
            Circle()
                .fill(RadialGradient(
                    colors: [TC.ink.opacity(0.80), TC.ink.opacity(0.62)],
                    center: UnitPoint(x: 0.4, y: 0.32),
                    startRadius: 0, endRadius: size * 0.6))

            // Track for the sweep.
            ArcShape(start: start, sweep: sweep, inset: size * 0.085)
                .stroke(TC.ink.opacity(0.55),
                        style: StrokeStyle(lineWidth: size * 0.09,
                                           lineCap: .round))

            // Red zone printed on the face, behind the live sweep.
            ArcShape(start: start + sweep * 0.86, sweep: sweep * 0.14,
                     inset: size * 0.085)
                .stroke(TC.cherry.opacity(0.35),
                        style: StrokeStyle(lineWidth: size * 0.09,
                                           lineCap: .butt))

            // Live sweep.
            ArcShape(start: start, sweep: sweep * fraction,
                     inset: size * 0.085)
                .stroke(LinearGradient(
                    colors: [arcColor.lighter(0.2), arcColor],
                    startPoint: .topLeading, endPoint: .bottomTrailing),
                    style: StrokeStyle(lineWidth: size * 0.09,
                                       lineCap: .round))

            // Turbo ring outside the bezel.
            ArcShape(start: start, sweep: sweep, inset: size * 0.012)
                .stroke(TC.ink.opacity(0.45),
                        style: StrokeStyle(lineWidth: size * 0.035,
                                           lineCap: .round))
            ArcShape(start: start, sweep: sweep * Double(min(1, max(0, boost))),
                     inset: size * 0.012)
                .stroke(LinearGradient(colors: [TC.turbo, Color(hex: "#7df9ff")],
                                       startPoint: .leading,
                                       endPoint: .trailing),
                        style: StrokeStyle(lineWidth: size * 0.035,
                                           lineCap: .round))

            TickRing(start: start, sweep: sweep, majorEvery: 4, count: 24)
                .stroke(.white.opacity(0.55), lineWidth: size * 0.012)
                .padding(size * 0.17)

            // Bezel.
            Circle()
                .strokeBorder(TC.metalGradient(TC.chrome), lineWidth: size * 0.03)
            Circle()
                .inset(by: size * 0.14)
                .strokeBorder(.white.opacity(0.10), lineWidth: 1)

            NeedleShape()
                .fill(LinearGradient(colors: [TC.cherry.lighter(0.25),
                                              TC.cherry.darker(0.05)],
                                     startPoint: .top, endPoint: .bottom))
                .frame(width: size * 0.085, height: size * 0.46)
                .offset(y: -size * 0.19)
                .rotationEffect(.degrees(needleAngle - 90))
                .animation(.interactiveSpring(response: 0.16,
                                              dampingFraction: 0.55),
                           value: needleAngle)

            // Hub with the readout.
            ZStack {
                Circle().fill(TC.metalGradient(TC.chrome))
                    .frame(width: size * 0.44, height: size * 0.44)
                Circle().fill(TC.ink.opacity(0.85))
                    .frame(width: size * 0.40, height: size * 0.40)
                VStack(spacing: -size * 0.02) {
                    Text(verbatim: "\(Int(max(0, speed)))")
                        .font(TC.readout(size * 0.22))
                        .foregroundStyle(.white)
                    Text("km/h")
                        .font(TC.label(size * 0.072))
                        .foregroundStyle(.white.opacity(0.65))
                }
            }
        }
        .frame(width: size, height: size)
        .shadow(color: .black.opacity(0.35), radius: 6, y: 3)
    }
}

/// An arc of a circle, given as a start angle and a sweep in degrees, with the
/// zero point at three o'clock and angles running clockwise.
struct ArcShape: Shape {
    var start: Double
    var sweep: Double
    var inset: CGFloat = 0

    func path(in rect: CGRect) -> Path {
        let r = min(rect.width, rect.height) / 2 - inset
        let c = CGPoint(x: rect.midX, y: rect.midY)
        var p = Path()
        p.addArc(center: c, radius: max(0.1, r),
                 startAngle: .degrees(start),
                 endAngle: .degrees(start + sweep),
                 clockwise: false)
        return p
    }
}

/// Tick marks around an arc. Every `majorEvery`-th tick is longer.
struct TickRing: Shape {
    var start: Double
    var sweep: Double
    var majorEvery: Int
    var count: Int

    func path(in rect: CGRect) -> Path {
        let r = min(rect.width, rect.height) / 2
        let c = CGPoint(x: rect.midX, y: rect.midY)
        var p = Path()
        for i in 0...count {
            let t = Double(i) / Double(count)
            let a = (start + sweep * t) * .pi / 180
            let major = i % majorEvery == 0
            let len = r * (major ? 0.22 : 0.11)
            let x0 = c.x + CGFloat(cos(a)) * (r - len)
            let y0 = c.y + CGFloat(sin(a)) * (r - len)
            let x1 = c.x + CGFloat(cos(a)) * r
            let y1 = c.y + CGFloat(sin(a)) * r
            p.move(to: CGPoint(x: x0, y: y0))
            p.addLine(to: CGPoint(x: x1, y: y1))
        }
        return p
    }
}

/// A needle: wide at the hub, pointed at the tip, with a counterweight tail.
struct NeedleShape: Shape {
    func path(in r: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: r.midX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - r.height * 0.18))
        p.addLine(to: CGPoint(x: r.midX + r.width * 0.18, y: r.maxY))
        p.addLine(to: CGPoint(x: r.midX - r.width * 0.18, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX, y: r.maxY - r.height * 0.18))
        p.closeSubpath()
        return p
    }
}

// ---------------------------------------------------------------- fuel

/// The fuel gauge. Segmented rather than continuous so the player can count
/// what is left, with the last two segments printed red - the reserve. On
/// `RaceEngine.lowFuelFraction` the whole gauge pulses - the same threshold
/// the screen wash and the on-screen message use, so all three warnings
/// arrive together.
struct FuelGauge: View {
    var fuel: Float             // 0…1
    var width: CGFloat = 118
    var segments: Int = 12
    /// Screen scale. The HUD is laid out in points that suit a phone in
    /// landscape; on a tablet everything is stepped up together so the dials
    /// stay the same size relative to the road rather than shrinking into the
    /// corners. See `HUDView.scale`.
    var scale: CGFloat = 1
    /// The warning is information and stays; the breathing is decoration and
    /// goes. `HUDView`'s screen-wide wash already worked this way
    /// (`LowFuelPulse(animated:)`) - this gauge was the half of the same
    /// warning that did not, so Reduce Motion silenced the wash and left the
    /// instrument flashing.
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    @State private var pulse = false

    private var low: Bool { fuel < RaceEngine.lowFuelFraction }
    /// Whether the pulse should be running at all. Not just `low`: the
    /// animation used to be started unconditionally in `onAppear`, so a
    /// `repeatForever` ran for the whole race on a view that redraws every
    /// simulation frame, on a full tank and under Reduce Motion alike.
    private var breathing: Bool { low && !reduceMotion }
    /// The lit state. Under Reduce Motion the warning is held at its bright
    /// end rather than at its dim one - still, but on.
    private var lit: Bool { low && (reduceMotion || pulse) }

    var body: some View {
        HStack(spacing: 7 * scale) {
            ZStack {
                Circle().fill(TC.ink.opacity(0.55))
                Circle().strokeBorder(.white.opacity(0.22), lineWidth: 1)
                Image(systemName: "fuelpump.fill")
                    .font(.system(size: 11 * scale, weight: .black))
                    .foregroundStyle(low ? TC.cherry : .white)
            }
            .frame(width: 24 * scale, height: 24 * scale)
            .scaleEffect(lit ? 1.12 : 1)

            HStack(spacing: 2) {
                ForEach(0..<segments, id: \.self) { i in
                    let full = Float(i) < fuel * Float(segments)
                    let reserve = i < 2
                    RoundedRectangle(cornerRadius: 1.5, style: .continuous)
                        .fill(full
                              ? AnyShapeStyle(LinearGradient(
                                    colors: reserve
                                        ? [TC.cherry.lighter(0.2), TC.cherry]
                                        : [TC.mint.lighter(0.22), TC.mint],
                                    startPoint: .top, endPoint: .bottom))
                              : AnyShapeStyle(reserve
                                    ? TC.cherry.opacity(0.28)
                                    : Color.white.opacity(0.14)))
                        .frame(height: 12 * scale)
                }
            }
            .frame(width: width * scale)
        }
        .padding(.horizontal, 8 * scale).padding(.vertical, 5 * scale)
        .background {
            GlassSurface(corner: 14 * scale, opacity: lit ? 0.62 : 0.44)
                .overlay {
                    if low {
                        RoundedRectangle(cornerRadius: 14 * scale,
                                         style: .continuous)
                            .strokeBorder(TC.cherry.opacity(lit ? 0.9 : 0.35),
                                          lineWidth: 1.6)
                    }
                }
        }
        // Started and stopped by the tank, not once and for ever on appear:
        // a race begins on a full one, so `onAppear` alone armed a
        // `repeatForever` that had nothing to show and then never stopped.
        .onAppear { setPulse() }
        .onChange(of: breathing) { _, _ in setPulse() }
    }

    private func setPulse() {
        // Assigning without an animation is what takes a running
        // `repeatForever` back off again.
        pulse = false
        guard breathing else { return }
        withAnimation(.easeInOut(duration: 0.55)
            .repeatForever(autoreverses: true)) {
            pulse = true
        }
    }
}

// ---------------------------------------------------------------- position

/// The position plate: the big number the player checks most often. Stamped
/// out of the metal of the place currently held, so first, second and third
/// are recognisable by colour alone before the digit is read.
struct PositionPlate: View {
    var position: Int
    var fieldSize: Int
    var lap: Int
    var totalLaps: Int
    var scale: CGFloat = 1

    private var metal: TC.Metal { TC.medal(for: position) ?? TC.chrome }

    var body: some View {
        HStack(spacing: 10 * scale) {
            // Position.
            ZStack {
                RoundedRectangle(cornerRadius: 12 * scale, style: .continuous)
                    .fill(TC.metalGradient(metal))
                RoundedRectangle(cornerRadius: 12 * scale, style: .continuous)
                    .strokeBorder(metal.dark.opacity(0.8), lineWidth: 1.2)
                HStack(alignment: .firstTextBaseline, spacing: 0) {
                    Text(verbatim: "\(position)")
                        .font(TC.readout(34 * scale))
                        .foregroundStyle(TC.ink)
                        .contentTransition(.numericText())
                    Text(verbatim: ordinalSuffix(position))
                        .font(TC.title(13 * scale))
                        .foregroundStyle(TC.ink.opacity(0.65))
                        .offset(y: -2 * scale)
                }
                .hardShadow(1, .white.opacity(0.45))
            }
            .frame(width: 62 * scale, height: 46 * scale)

            VStack(alignment: .leading, spacing: 3 * scale) {
                Text(verbatim: "/ \(fieldSize)")
                    .font(TC.readout(12 * scale, .bold))
                    .foregroundStyle(.white.opacity(0.8))
                // Lap pips: filled for laps done, hollow for laps to come.
                HStack(spacing: 3 * scale) {
                    Text("LAP")
                        .font(TC.label(9 * scale))
                        .foregroundStyle(.white.opacity(0.7))
                    ForEach(0..<max(1, totalLaps), id: \.self) { i in
                        Capsule()
                            .fill(i < lap ? Color.white
                                          : Color.white.opacity(0.25))
                            .frame(width: (i == lap - 1 ? 12 : 6) * scale,
                                   height: 4 * scale)
                    }
                }
                .animation(.spring(response: 0.35, dampingFraction: 0.7),
                           value: lap)
            }
        }
        .padding(.leading, 6 * scale).padding(.trailing, 12 * scale)
        .padding(.vertical, 6 * scale)
        .background(GlassSurface(corner: 18 * scale))
    }
}

// ---------------------------------------------------------------- lap timer

/// The timing panel: running race time, the last lap with its delta against
/// the player's best, and the best itself. A green delta means the last lap
/// was quicker - the single most useful number a racer can be given.
struct LapTimerPanel: View {
    var raceTime: Double
    var lastLap: Double
    var bestLap: Double
    var scale: CGFloat = 1

    private var delta: Double? {
        guard lastLap > 0, bestLap.isFinite, lastLap != bestLap else {
            return nil
        }
        return lastLap - bestLap
    }

    var body: some View {
        VStack(spacing: 1) {
            // Before the flag drops the clock reads zero rather than dashes:
            // dashes look like a fault, and the panel has to look alive on
            // the grid.
            Text(verbatim: raceTime > 0 ? formatTime(raceTime) : "0.000")
                .font(TC.readout(21 * scale))
                .foregroundStyle(.white)
            HStack(spacing: 6 * scale) {
                if lastLap > 0 {
                    Text(verbatim: formatTime(lastLap))
                        .font(TC.readout(11 * scale, .bold))
                        .foregroundStyle(.white.opacity(0.75))
                }
                if let d = delta {
                    Text(verbatim: formatDelta(d))
                        .font(TC.readout(11 * scale))
                        .foregroundStyle(d < 0 ? TC.mint : TC.rose)
                } else if bestLap.isFinite {
                    HStack(spacing: 3 * scale) {
                        Image(systemName: "stopwatch.fill")
                            .font(.system(size: 8 * scale, weight: .black))
                        Text(verbatim: formatTime(bestLap))
                            .font(TC.readout(11 * scale, .bold))
                    }
                    .foregroundStyle(TC.mint)
                }
            }
        }
        .padding(.horizontal, 14 * scale).padding(.vertical, 6 * scale)
        .background(GlassSurface(corner: 16 * scale))
    }
}
