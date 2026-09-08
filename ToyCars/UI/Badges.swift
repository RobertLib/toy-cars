//
//  Badges.swift
//  ToyCars
//
//  The small printed and stamped pieces: coin counters, medals, star ratings,
//  ribbons, price tags, stat meters. These carry most of the information in the
//  menus, so each one is built to be legible at a glance from arm's length -
//  strong silhouette first, detail second.
//

import SwiftUI

// ---------------------------------------------------------------- coins

/// The coin counter. The coin itself is a struck disc - rim, face, embossed
/// mark - and it flips when the amount changes so a reward is never missed.
struct CoinPill: View {
    var coins: Int
    var size: CGFloat = 20

    @State private var flip: Double = 0
    @State private var bump: CGFloat = 1

    var body: some View {
        HStack(spacing: 7) {
            CoinFace(size: size)
                .rotation3DEffect(.degrees(flip), axis: (x: 0, y: 1, z: 0))
            Text(verbatim: "\(coins)")
                .font(TC.readout(size * 0.85))
                .foregroundStyle(TC.ink)
                .contentTransition(.numericText())
        }
        .padding(.horizontal, 12).padding(.vertical, 6)
        .background {
            ZStack {
                Capsule().fill(TC.plasticGradient(TC.cream))
                Gloss(shape: Capsule(), strength: 0.5, extent: 0.5)
                Capsule().strokeBorder(
                    LinearGradient(colors: [.white, TC.sun.opacity(0.55)],
                                   startPoint: .top, endPoint: .bottom),
                    lineWidth: 1.4)
            }
            .compositingGroup()
            .lift(7)
        }
        .scaleEffect(bump)
        .onChange(of: coins) { _, _ in
            withAnimation(.spring(response: 0.5, dampingFraction: 0.6)) {
                flip += 360
            }
            withAnimation(.spring(response: 0.22, dampingFraction: 0.45)) {
                bump = 1.14
            }
            withAnimation(.spring(response: 0.4, dampingFraction: 0.6)
                .delay(0.14)) {
                bump = 1
            }
        }
    }
}

/// A struck coin: dark rim, gold field, embossed star.
struct CoinFace: View {
    var size: CGFloat = 20

    var body: some View {
        ZStack {
            Circle().fill(TC.metalGradient(TC.gold))
            Circle().strokeBorder(TC.gold.dark.opacity(0.9),
                                  lineWidth: size * 0.07)
            Circle().inset(by: size * 0.16)
                .strokeBorder(TC.gold.light.opacity(0.6),
                              lineWidth: size * 0.05)
            Image(systemName: "star.fill")
                .font(.system(size: size * 0.40, weight: .black))
                .foregroundStyle(TC.gold.dark.opacity(0.55))
                .shadow(color: TC.gold.light.opacity(0.8), radius: 0, y: -0.5)
        }
        .frame(width: size, height: size)
        .shadow(color: TC.gold.dark.opacity(0.5), radius: 1, y: 1)
    }
}

// ---------------------------------------------------------------- medals

/// A finishing medal: metal disc, fluted rim, engraved position, ribbon tails.
struct MedalBadge: View {
    var position: Int
    var size: CGFloat = 64
    var showRibbon: Bool = true

    private var metal: TC.Metal { TC.medal(for: position) ?? TC.chrome }

    var body: some View {
        ZStack {
            if showRibbon {
                // Two ribbon tails behind the disc.
                HStack(spacing: size * 0.18) {
                    RibbonTail(color: TC.cherry).rotationEffect(.degrees(-12))
                    RibbonTail(color: TC.cherry.darker(0.12))
                        .rotationEffect(.degrees(12))
                }
                .frame(width: size * 0.9, height: size * 0.75)
                .offset(y: size * 0.42)
            }
            ZStack {
                Circle().fill(TC.metalGradient(metal))
                // Fluting: short radial ticks around the rim.
                ForEach(0..<24, id: \.self) { i in
                    Capsule()
                        .fill(metal.dark.opacity(0.22))
                        .frame(width: size * 0.022, height: size * 0.10)
                        .offset(y: -size * 0.44)
                        .rotationEffect(.degrees(Double(i) / 24 * 360))
                }
                Circle().inset(by: size * 0.13)
                    .strokeBorder(metal.dark.opacity(0.5),
                                  lineWidth: size * 0.03)
                Circle().inset(by: size * 0.17)
                    .fill(RadialGradient(
                        colors: [metal.light.opacity(0.6),
                                 metal.body.opacity(0.1)],
                        center: UnitPoint(x: 0.35, y: 0.28),
                        startRadius: 0, endRadius: size * 0.4))
                StrokeText(text: Text(verbatim: "\(position)")
                    .font(TC.title(size * 0.46)),
                           width: size * 0.022,
                           color: metal.dark.opacity(0.85),
                           dropShadow: 0)
                    .foregroundStyle(metal.light)
                Sheen(shape: Circle(), period: 5.0, strength: 0.45)
            }
            .frame(width: size, height: size)
            .compositingGroup()
            .shadow(color: metal.dark.opacity(0.55), radius: size * 0.06,
                    y: size * 0.05)
            .shadow(color: TC.ambient, radius: size * 0.2, y: size * 0.12)
        }
    }
}

private struct RibbonTail: View {
    var color: Color
    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            Path { p in
                p.move(to: CGPoint(x: 0, y: 0))
                p.addLine(to: CGPoint(x: w, y: 0))
                p.addLine(to: CGPoint(x: w * 0.72, y: h))
                p.addLine(to: CGPoint(x: w * 0.5, y: h * 0.74))
                p.addLine(to: CGPoint(x: w * 0.28, y: h))
                p.closeSubpath()
            }
            .fill(LinearGradient(colors: [color.lighter(0.14), color.darker(0.1)],
                                 startPoint: .top, endPoint: .bottom))
        }
    }
}

/// Star rating out of three - the arcade shorthand for how well a track went.
struct StarRating: View {
    var earned: Int
    var total: Int = 3
    var size: CGFloat = 18
    /// Stars pop in one after another when the view appears.
    var animated: Bool = false

    @State private var shown = 0

    var body: some View {
        HStack(spacing: size * 0.16) {
            ForEach(0..<total, id: \.self) { i in
                let on = i < (animated ? shown : earned)
                Image(systemName: "star.fill")
                    .font(.system(size: size, weight: .black))
                    .foregroundStyle(on
                        ? AnyShapeStyle(TC.metalGradient(TC.gold))
                        : AnyShapeStyle(TC.ink.opacity(0.18)))
                    .overlay {
                        if on {
                            Image(systemName: "star.fill")
                                .font(.system(size: size, weight: .black))
                                .foregroundStyle(.white.opacity(0.45))
                                .mask(Image(systemName: "star.fill")
                                    .font(.system(size: size, weight: .black))
                                    .offset(y: -size * 0.3))
                        }
                    }
                    .shadow(color: on ? TC.gold.dark.opacity(0.6) : .clear,
                            radius: 0, y: 1)
                    .scaleEffect(on ? 1 : 0.86)
                    .animation(.spring(response: 0.34, dampingFraction: 0.5),
                               value: on)
            }
        }
        .task {
            guard animated else { return }
            // A task rather than a queue of delayed blocks: the view is often
            // created while a screen transition is still running, and a task
            // is tied to the view's lifetime instead of firing into the void.
            try? await Task.sleep(for: .milliseconds(260))
            for i in 0..<earned {
                shown = i + 1
                Haptics.tap()
                GameAudio.shared.pickup()
                try? await Task.sleep(for: .milliseconds(180))
            }
        }
    }
}

/// Difficulty as filled flames on a plate.
struct DifficultyFlames: View {
    var level: Int
    var total: Int = 3
    var size: CGFloat = 14

    var body: some View {
        HStack(spacing: 2) {
            ForEach(0..<total, id: \.self) { i in
                Image(systemName: i < level ? "flame.fill" : "flame")
                    .font(.system(size: size, weight: .black))
                    .foregroundStyle(i < level
                        ? AnyShapeStyle(LinearGradient(
                            colors: [TC.sun, TC.cherry],
                            startPoint: .top, endPoint: .bottom))
                        : AnyShapeStyle(TC.ink.opacity(0.18)))
            }
        }
    }
}

// ---------------------------------------------------------------- labels

/// The performance class of a car, worked out from its stats: a letter on a
/// coloured shield, the way every racing game grades its cars.
struct ClassBadge: View {
    var letter: String
    var color: Color
    var size: CGFloat = 34

    var body: some View {
        ZStack {
            ShieldShape()
                .fill(TC.plasticGradient(color))
            ShieldShape()
                .strokeBorder(.white.opacity(0.75), lineWidth: 1.6)
            Text(verbatim: letter)
                .font(TC.title(size * 0.52))
                .foregroundStyle(.white)
                .hardShadow(1, TC.ink.opacity(0.5))
        }
        .frame(width: size, height: size * 1.15)
        .shadow(color: TC.contact, radius: 3, y: 2)
    }
}

struct ShieldShape: InsettableShape {
    var insetAmount: CGFloat = 0

    func path(in rect: CGRect) -> Path {
        let r = rect.insetBy(dx: insetAmount, dy: insetAmount)
        var p = Path()
        let c = r.width * 0.22
        p.move(to: CGPoint(x: r.minX, y: r.minY + c))
        p.addQuadCurve(to: CGPoint(x: r.minX + c, y: r.minY),
                       control: CGPoint(x: r.minX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX - c, y: r.minY))
        p.addQuadCurve(to: CGPoint(x: r.maxX, y: r.minY + c),
                       control: CGPoint(x: r.maxX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - r.height * 0.30))
        p.addQuadCurve(to: CGPoint(x: r.midX, y: r.maxY),
                       control: CGPoint(x: r.maxX, y: r.maxY - r.height * 0.06))
        p.addQuadCurve(to: CGPoint(x: r.minX, y: r.maxY - r.height * 0.30),
                       control: CGPoint(x: r.minX, y: r.maxY - r.height * 0.06))
        p.closeSubpath()
        return p
    }

    func inset(by amount: CGFloat) -> ShieldShape {
        ShieldShape(insetAmount: insetAmount + amount)
    }
}

/// A cardboard price tag with a punched hole - what a toy car costs.
struct PriceTag: View {
    var price: Int
    var affordable: Bool

    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .strokeBorder(TC.ink.opacity(0.28), lineWidth: 2)
                .frame(width: 9, height: 9)
            CoinFace(size: 17)
            Text(verbatim: "\(price)")
                .font(TC.readout(18))
                .foregroundStyle(affordable ? TC.ink : TC.cherry)
        }
        .padding(.leading, 9).padding(.trailing, 13)
        .padding(.vertical, 7)
        .background {
            ZStack {
                TagShape().fill(TC.plasticGradient(TC.cream))
                TagShape().strokeBorder(TC.ink.opacity(0.18), lineWidth: 1.3)
            }
            .compositingGroup()
            .shadow(color: TC.contact, radius: 4, y: 3)
        }
    }
}

struct TagShape: InsettableShape {
    var insetAmount: CGFloat = 0

    func path(in rect: CGRect) -> Path {
        let r = rect.insetBy(dx: insetAmount, dy: insetAmount)
        let notch = r.height * 0.42
        var p = Path()
        p.move(to: CGPoint(x: r.minX + notch, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX - 8, y: r.minY))
        p.addQuadCurve(to: CGPoint(x: r.maxX, y: r.minY + 8),
                       control: CGPoint(x: r.maxX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - 8))
        p.addQuadCurve(to: CGPoint(x: r.maxX - 8, y: r.maxY),
                       control: CGPoint(x: r.maxX, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX + notch, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX, y: r.midY))
        p.closeSubpath()
        return p
    }

    func inset(by amount: CGFloat) -> TagShape {
        TagShape(insetAmount: insetAmount + amount)
    }
}

/// A banner ribbon for a screen or card title - the name sits on a folded
/// band with darker fold ends, which is what makes a title feel placed rather
/// than typed.
struct RibbonBanner: View {
    var text: String
    var color: Color = TC.cherry
    var size: CGFloat = 22
    var textColor: Color = .white

    var body: some View {
        StrokeText(text: Text(verbatim: text).font(TC.title(size)),
                   width: 1.5, color: color.darker(0.30), dropShadow: 1.5)
            .foregroundStyle(textColor)
            .padding(.horizontal, size * 0.9)
            .padding(.vertical, size * 0.28)
            .background {
                ZStack {
                    RibbonShape().fill(TC.plasticGradient(color))
                    RibbonShape().stroke(color.darker(0.28), lineWidth: 1.4)
                    Gloss(shape: RibbonShape(), strength: 0.35, extent: 0.45)
                }
                .compositingGroup()
                .shadow(color: TC.contact, radius: 4, y: 3)
            }
    }
}

struct RibbonShape: Shape {
    func path(in r: CGRect) -> Path {
        let notch = r.height * 0.34
        var p = Path()
        p.move(to: CGPoint(x: r.minX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX - notch, y: r.midY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX + notch, y: r.midY))
        p.closeSubpath()
        return p
    }
}

// ---------------------------------------------------------------- meters

/// A car statistic as a row of lit segments. Segments read faster than a
/// continuous bar because the eye can count them, and they are how the genre
/// shows stats - so a player already knows what they mean.
struct SegmentMeter: View {
    var label: LocalizedStringKey
    var value: Float            // 0…1
    var color: Color
    var icon: String
    var segments: Int = 10
    var labelWidth: CGFloat = 78
    /// Point sizes are multiplied by this. See `Metrics`.
    var scale: CGFloat = 1

    var body: some View {
        HStack(spacing: 8 * scale) {
            Image(systemName: icon)
                .font(.system(size: 11 * scale, weight: .black))
                .foregroundStyle(color)
                .frame(width: 14 * scale)
            Text(label)
                .font(TC.label(10 * scale))
                .foregroundStyle(TC.inkSoft)
                .lineLimit(1)
                .minimumScaleFactor(0.75)
                .frame(width: labelWidth * scale, alignment: .leading)
            HStack(spacing: 2.5 * scale) {
                ForEach(0..<segments, id: \.self) { i in
                    let lit = Float(i) < value * Float(segments)
                    // The last segments are hotter, so a top-tier stat looks
                    // it even before the count registers.
                    let tone = i >= segments - 2 ? color.lighter(0.18) : color
                    RoundedRectangle(cornerRadius: 2 * scale,
                                     style: .continuous)
                        .fill(lit ? AnyShapeStyle(LinearGradient(
                                colors: [tone.lighter(0.22), tone],
                                startPoint: .top, endPoint: .bottom))
                            : AnyShapeStyle(TC.ink.opacity(0.10)))
                        .frame(height: 11 * scale)
                        .overlay {
                            if lit {
                                RoundedRectangle(cornerRadius: 2 * scale,
                                                 style: .continuous)
                                    .fill(.white.opacity(0.35))
                                    .frame(height: 3.5 * scale)
                                    .offset(y: -3 * scale)
                            }
                        }
                        .shadow(color: lit ? color.opacity(0.4) : .clear,
                                radius: 2, y: 1)
                }
            }
            .animation(.spring(response: 0.4, dampingFraction: 0.8),
                       value: value)
        }
    }
}

/// A row of trophies showing overall campaign progress.
struct TrophyRow: View {
    /// One entry per track: true if the track has been completed.
    var completed: [Bool]
    var size: CGFloat = 22

    var body: some View {
        HStack(spacing: size * 0.34) {
            ForEach(Array(completed.enumerated()), id: \.offset) { _, done in
                ZStack {
                    Circle()
                        .fill(done
                              ? AnyShapeStyle(TC.metalGradient(TC.gold))
                              : AnyShapeStyle(Color.white.opacity(0.14)))
                    Circle().strokeBorder(done ? TC.gold.dark.opacity(0.7)
                                               : .white.opacity(0.28),
                                          lineWidth: 1.4)
                    Image(systemName: "trophy.fill")
                        .font(.system(size: size * 0.48, weight: .black))
                        .foregroundStyle(done ? TC.gold.dark.opacity(0.75)
                                              : .white.opacity(0.45))
                }
                .frame(width: size * 1.6, height: size * 1.6)
                .shadow(color: done ? TC.gold.body.opacity(0.5) : .clear,
                        radius: 6)
            }
        }
    }
}

/// A key/value line on a panel, separated by a dotted leader like a printed
/// results sheet.
struct StatRow: View {
    var key: LocalizedStringKey
    var value: String
    var highlight: Bool = false
    var icon: String? = nil
    /// Point sizes are multiplied by this, so the same row can serve a phone
    /// and a tablet. See `Metrics`.
    var scale: CGFloat = 1

    var body: some View {
        HStack(spacing: 7 * scale) {
            if let icon {
                Image(systemName: icon)
                    .font(.system(size: 10 * scale, weight: .black))
                    .foregroundStyle(TC.inkSoft.opacity(0.7))
                    .frame(width: 13 * scale)
            }
            Text(key)
                .font(TC.label(11 * scale))
                .foregroundStyle(TC.inkSoft)
                .lineLimit(1)
                .minimumScaleFactor(0.85)
                // The leader is the only thing here that should give way when
                // the row is tight. Without these priorities SwiftUI splits
                // the slack evenly between the label and the leader, and the
                // label ends up truncated next to eighty points of dots.
                .layoutPriority(1)
            Rectangle()
                .fill(TC.ink.opacity(0.12))
                .frame(height: 1)
                .mask(HStack(spacing: 3 * scale) {
                    ForEach(0..<40, id: \.self) { _ in
                        Circle().frame(width: 1.5 * scale, height: 1.5 * scale)
                    }
                })
                .layoutPriority(-1)
            Text(verbatim: value)
                .font(TC.readout(15 * scale))
                // A personal best is good news; red would read as a warning.
                .foregroundStyle(highlight ? TC.teal : TC.ink)
                .lineLimit(1)
        }
    }
}

// ---------------------------------------------------------------- tiles

/// One figure on its own tile, label above value.
///
/// This is the shape a number takes when it is one of a row of three read
/// together - a race time beside a best lap beside a payout. The labels go
/// above in small caps rather than beside the value, so once a player knows
/// the row they can skip the labels entirely and read the three figures as a
/// line; a label to the left of each value would force the eye to zig-zag.
///
/// A figure that is news - a record just broken, a reward just paid - takes a
/// coloured `rim`, and `valueTint` recolours the number itself. They are kept
/// apart because the two do not always go together: a payout wants a gold rim
/// but a gold number would be unreadable on cream.
struct StatTile: View {
    var label: LocalizedStringKey
    var value: String
    /// An SF Symbol beside the label. Ignored when `coin` is set.
    var icon: String? = nil
    /// Draw a struck coin instead of a symbol - for a payout.
    var coin: Bool = false
    var rim: Color? = nil
    var valueTint: Color? = nil
    var scale: CGFloat = 1

    var body: some View {
        let corner = 16 * scale
        return VStack(spacing: 2 * scale) {
            HStack(spacing: 4 * scale) {
                if coin {
                    CoinFace(size: 12 * scale)
                } else if let icon {
                    Image(systemName: icon)
                        .font(.system(size: 9 * scale, weight: .black))
                }
                Text(label)
                    .font(TC.label(10 * scale))
                    .lineLimit(1)
            }
            .foregroundStyle(TC.inkSoft)

            Text(verbatim: value)
                .font(TC.readout(21 * scale))
                .foregroundStyle(valueTint ?? TC.ink)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity)
        .padding(.horizontal, 8 * scale)
        .padding(.vertical, 10 * scale)
        .background {
            ZStack {
                PanelSurface(fill: TC.cream, corner: corner)
                if let rim {
                    RoundedRectangle(cornerRadius: corner, style: .continuous)
                        .strokeBorder(rim, lineWidth: 1.8 * scale)
                }
            }
        }
    }
}
