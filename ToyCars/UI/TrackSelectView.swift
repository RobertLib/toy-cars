//
//  TrackSelectView.swift
//  ToyCars
//
//  Choosing a circuit. The card on the left is built as a travel poster for
//  the place - its own sky, its own light, its own ground - with the circuit
//  laid across it as a ribbon of road. The whole screen changes weather when
//  the selection changes, which is what makes three tracks feel like three
//  destinations rather than three rows in a list.
//
//  The panel on the right answers, in order: how hard, how did I do last time,
//  what do I get. Records get a medal, because a personal best is worth more
//  on screen than in a font size.
//
//  Landscape leaves this screen about 240 points for the poster-and-panel
//  band once the header, the track strip and the screen margins have taken
//  their share, and that budget is what decides the panel's contents. Fitting
//  a panel that breathes into it meant taking things out rather than closing
//  the gaps up, so everything that was already on screen somewhere else went:
//
//  * the circuit length - the rubber stamp on the poster gives it;
//  * the best position - the medal beside the track name *is* it;
//  * the pagination dots - the strip underneath says the same thing, with
//    names and lock states attached.
//
//  What is left is grouped - identity, record, prize, action - with gaps
//  between the groups wider than the gaps inside them. Vertical padding runs
//  tighter than horizontal throughout: landscape is short of height and long
//  on width, so the padding budget is spent where there is room for it.
//

import SwiftUI

struct TrackSelectView: View {
    @Bindable var app: AppModel
    private let tracks = TrackCatalog.shared.tracks
    @State private var index = 0

    private var track: TrackDef? {
        tracks.indices.contains(index) ? tracks[index] : nil
    }
    private var unlocked: Bool {
        track.map { app.progress.isUnlocked($0) } ?? false
    }
    private var palette: ScenePalette {
        ScenePalette.forTheme(track?.theme)
    }

    var body: some View {
        ZStack {
            Backdrop(palette: palette, showRoad: false, vignette: 0.5)
                .animation(.easeInOut(duration: 0.5), value: index)

            if let track {
                GeometryReader { geo in
                    let m = Metrics(geo.size)
                    VStack(spacing: 0) {
                        header(m)
                            .frame(height: m.pt(42))
                        Spacer(minLength: m.tight)
                        // The band takes whatever the header and the strip
                        // leave, and the poster and the panel both fill it,
                        // so the two always share a top and a bottom edge.
                        // Capped, because a tablet would otherwise hand the
                        // panel seven hundred points to put four lines of
                        // text in; past this height the band stops growing
                        // and the slack becomes margin above and below it.
                        HStack(alignment: .top, spacing: m.gutter) {
                            poster(track, m)
                            infoCard(track, m)
                                .frame(width: m.sidePanel)
                        }
                        .frame(maxHeight: m.pt(300))
                        Spacer(minLength: m.item)
                        trackStrip(m)
                    }
                    .padding(.horizontal, m.edge)
                    .padding(.vertical, m.edgeV)
                }
            }
        }
        .onAppear {
            if let i = app.debugTrackIndex, tracks.indices.contains(i) {
                index = i
            }
        }
        // Reading a circuit's centreline back off the disk measures 6 to 8 ms
        // - about half a frame - and `poster` asks `TrackShapeCache` for it
        // from inside `body`, so the first look at each circuit pays for it
        // mid-layout. That is one dropped frame on the way in, which is
        // unavoidable and barely visible; what is avoidable is paying it
        // *again* on every swipe between the three. So the ones the player is
        // not looking at yet are warmed while they read this one, one per
        // turn of the loop so the main thread is never held for two at once.
        .task {
            for t in tracks {
                _ = TrackShapeCache.shared.data(t.id)
                await Task.yield()
            }
        }
        .gesture(
            DragGesture(minimumDistance: 30)
                .onEnded { v in
                    if v.translation.width < -30 { step(1) }
                    if v.translation.width > 30 { step(-1) }
                }
        )
    }

    // ------------------------------------------------------------ header

    private func header(_ m: Metrics) -> some View {
        HStack {
            Button {
                Haptics.tap(); GameAudio.shared.uiTap(); app.go(.menu)
            } label: { Image(systemName: "chevron.left") }
                .buttonStyle(CircleIconButtonStyle(color: TC.cream,
                                                   size: m.btnIcon))
                .accessibilityLabel("Back to menu")
            Spacer()
            RibbonBanner(text: String(localized: "PICK A TRACK"),
                         color: TC.azure, size: m.pt(19))
            Spacer()
            CoinPill(coins: app.progress.state.coins, size: m.pt(20))
        }
    }

    // ------------------------------------------------------------ poster

    private func poster(_ t: TrackDef, _ m: Metrics) -> some View {
        let p = ScenePalette.forTheme(t.theme)
        // The caption and the stamp sit over the map, so the map is inset far
        // enough that the road never runs under either of them.
        let inset = m.pt(14)
        return ZStack {
            // The place, in miniature: sky, sun, hills, ground.
            PosterScene(palette: p)

            if let data = TrackShapeCache.shared.data(t.id) {
                TrackMiniMap(track: data, lineWidth: m.pt(13),
                             roadColor: roadColor(t),
                             edgeColor: vergeColor(t),
                             style: .map,
                             startS: data.file.startS)
                    .padding(.horizontal, inset + m.pt(14))
                    .padding(.top, inset + m.pt(26))
                    .padding(.bottom, inset + m.pt(14))
            }

            // Name on a ribbon, top left - a poster caption.
            VStack {
                HStack(alignment: .top) {
                    RibbonBanner(text: t.name.uppercased(),
                                 color: unlocked ? TC.cherry : TC.steel,
                                 size: m.pt(15))
                    Spacer()
                    stamp(t, m)
                }
                Spacer()
            }
            .padding(inset)

            if !unlocked { lockedVeil(m) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .clipShape(RoundedRectangle(cornerRadius: m.rCard,
                                    style: .continuous))
        .overlay {
            // A cream mount around the poster, like a framed print.
            RoundedRectangle(cornerRadius: m.rCard, style: .continuous)
                .strokeBorder(LinearGradient(
                    colors: [.white.opacity(0.9), .white.opacity(0.35)],
                    startPoint: .top, endPoint: .bottom), lineWidth: 3)
        }
        .lift(m.pt(10))
    }

    /// The rubber stamp in the corner: distance and difficulty, the two facts
    /// that decide whether a player is ready for this circuit.
    private func stamp(_ t: TrackDef, _ m: Metrics) -> some View {
        let r = m.pt(11)
        return VStack(spacing: m.hair) {
            Text(verbatim: "\(Int(t.length)) m")
                .font(TC.readout(m.pt(14)))
                .foregroundStyle(TC.ink)
            DifficultyFlames(level: t.difficulty, size: m.pt(12))
        }
        .padding(.horizontal, m.pt(11)).padding(.vertical, m.pt(7))
        .background {
            ZStack {
                RoundedRectangle(cornerRadius: r, style: .continuous)
                    .fill(TC.cream.opacity(0.92))
                RoundedRectangle(cornerRadius: r, style: .continuous)
                    .strokeBorder(TC.ink.opacity(0.25), lineWidth: 1.4)
                RoundedRectangle(cornerRadius: r, style: .continuous)
                    .inset(by: 3)
                    .strokeBorder(TC.ink.opacity(0.12), lineWidth: 0.8)
            }
        }
        .rotationEffect(.degrees(3))
        .shadow(color: TC.contact, radius: 3, y: 2)
    }

    private func lockedVeil(_ m: Metrics) -> some View {
        ZStack {
            Rectangle().fill(TC.ink.opacity(0.62))
            VStack(spacing: m.item) {
                ZStack {
                    Circle().fill(TC.ink.opacity(0.8))
                    Circle().strokeBorder(TC.sun.opacity(0.75), lineWidth: 2)
                    Image(systemName: "lock.fill")
                        .font(.system(size: m.pt(26), weight: .black))
                        .foregroundStyle(TC.sun)
                }
                .frame(width: m.pt(62), height: m.pt(62))
                Text("Finish the previous track in the TOP 3")
                    .font(TC.body(m.pt(13), .black))
                    .foregroundStyle(.white.opacity(0.9))
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, m.section)
            }
        }
    }

    // ------------------------------------------------------------ info

    private func infoCard(_ t: TrackDef, _ m: Metrics) -> some View {
        let rec = app.progress.record(t.id)
        let btn = m.pt(47)
        return VStack(alignment: .leading, spacing: 0) {
            // -- identity
            HStack(alignment: .top, spacing: m.snug) {
                VStack(alignment: .leading, spacing: m.hair) {
                    Text(verbatim: t.name)
                        .font(TC.title(m.pt(23)))
                        .foregroundStyle(TC.ink)
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                    Text(t.localizedSubtitle)
                        .font(TC.body(m.pt(11), .semibold))
                        .foregroundStyle(TC.inkSoft)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
                if rec.bestPosition > 0 {
                    MedalBadge(position: rec.bestPosition, size: m.pt(38),
                               showRibbon: false)
                }
            }

            Rectangle().fill(TC.ink.opacity(0.08)).frame(height: 1)
                .padding(.vertical, m.snug)

            // -- record
            VStack(spacing: m.tight * 1.6) {
                StatRow(key: "Laps", value: "\(t.laps)", icon: "repeat",
                        scale: m.type)
                StatRow(key: "Best lap",
                        value: rec.bestLap.map(formatTime) ?? "—",
                        highlight: rec.bestLap != nil,
                        icon: "stopwatch", scale: m.type)
            }

            // -- the prize, presented as a prize
            reward(t, cleared: rec.completed, m)
                .padding(.top, m.snug)

            // Flexible, so the button is at the foot of the card whatever
            // the card's height turns out to be.
            Spacer(minLength: 0)

            // -- action
            if unlocked {
                Button {
                    Haptics.tap(); GameAudio.shared.uiTap()
                    app.startRace(track: t)
                } label: {
                    ActionLabel(title: "START", icon: "flag.checkered",
                                size: btn)
                }
                .buttonStyle(ChunkyButtonStyle(color: TC.lime, height: btn,
                                               sheen: true))
            } else {
                HStack(spacing: m.snug) {
                    Image(systemName: "lock.fill")
                        .font(.system(size: m.pt(14), weight: .black))
                    Text("LOCKED").font(TC.title(m.pt(16)))
                }
                .foregroundStyle(TC.inkSoft)
                .frame(maxWidth: .infinity)
                .frame(height: btn)
                .background(RoundedRectangle(cornerRadius: btn * 0.32,
                                             style: .continuous)
                    .fill(TC.ink.opacity(0.08)))
            }
        }
        .padding(.horizontal, m.pad)
        .padding(.vertical, m.snug * 1.6)
        .frame(maxHeight: .infinity)
        .background(PanelSurface(fill: TC.cream, corner: m.rCard))
    }

    private func reward(_ t: TrackDef, cleared: Bool,
                        _ m: Metrics) -> some View {
        let r = m.pt(13)
        return HStack(spacing: m.snug) {
            CoinFace(size: m.pt(19))
            VStack(alignment: .leading, spacing: -m.hair * 0.5) {
                Text("REWARD")
                    .font(TC.label(m.pt(9)))
                    .foregroundStyle(TC.ink.opacity(0.55))
                Text(verbatim: "\(t.reward)")
                    .font(TC.readout(m.pt(17)))
                    .foregroundStyle(TC.ink)
            }
            Spacer(minLength: m.tight)
            if cleared {
                HStack(spacing: m.tight) {
                    Image(systemName: "trophy.fill")
                        .font(.system(size: m.pt(10), weight: .black))
                    Text("CLEARED").font(TC.label(m.pt(9)))
                }
                .foregroundStyle(TC.ink.opacity(0.7))
            }
        }
        .padding(.horizontal, m.pt(11)).padding(.vertical, m.pt(7))
        .background(RoundedRectangle(cornerRadius: r, style: .continuous)
            .fill(TC.sun.opacity(0.20)))
        .overlay(RoundedRectangle(cornerRadius: r, style: .continuous)
            .strokeBorder(TC.sun.opacity(0.5), lineWidth: 1))
    }

    // ------------------------------------------------------------ strip

    /// The three circuits as a row of tabs. What made this strip feel packed
    /// was never its height - it was that the chips had 12 points of padding
    /// and sat 8 apart, so each name nearly touched its neighbour's and the
    /// selected capsule had no clear space of its own to sit in. Landscape is
    /// short of height and long on width, so the fix is bought sideways:
    /// generous padding inside each chip and a wide gap between them, for
    /// almost no vertical cost.
    private func trackStrip(_ m: Metrics) -> some View {
        HStack(spacing: m.snug * 1.5) {
            ForEach(Array(tracks.enumerated()), id: \.element.id) { i, t in
                let open = app.progress.isUnlocked(t)
                Button {
                    Haptics.tap(); GameAudio.shared.uiTap()
                    withAnimation(.spring(response: 0.35,
                                          dampingFraction: 0.8)) {
                        index = i
                    }
                } label: {
                    HStack(spacing: m.tight * 1.6) {
                        if !open {
                            Image(systemName: "lock.fill")
                                .font(.system(size: m.pt(10), weight: .black))
                        } else if app.progress.record(t.id).completed {
                            Image(systemName: "trophy.fill")
                                .font(.system(size: m.pt(10), weight: .black))
                                .foregroundStyle(TC.gold.body)
                        }
                        Text(verbatim: t.name.uppercased())
                            .font(TC.label(m.pt(12)))
                            .lineLimit(1)
                    }
                    .foregroundStyle(i == index ? TC.ink
                                                : .white.opacity(0.75))
                    .padding(.horizontal, m.pt(17))
                    .padding(.vertical, m.pt(8))
                    .background {
                        if i == index {
                            ZStack {
                                Capsule().fill(TC.plasticGradient(TC.cream))
                                Gloss(shape: Capsule(), strength: 0.4,
                                      extent: 0.5)
                            }
                            .shadow(color: TC.contact, radius: 3, y: 2)
                        }
                    }
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, m.snug)
        .padding(.vertical, m.tight * 1.6)
        .background(GlassSurface(corner: m.rPanel, opacity: 0.34))
    }

    // ------------------------------------------------------------ colours

    private func roadColor(_ t: TrackDef) -> Color {
        switch t.theme {
        case "beach": return Color(hex: "#cf9d5f")
        case "winter": return Color(hex: "#f2f7ff")
        default: return Color(hex: "#4a4f58")
        }
    }

    private func vergeColor(_ t: TrackDef) -> Color {
        switch t.theme {
        case "beach": return Color(hex: "#8a6434")
        case "winter": return Color(hex: "#9db6cf")
        default: return Color(hex: "#2b2f36")
        }
    }

    private func step(_ d: Int) {
        Haptics.tap(); GameAudio.shared.uiTap()
        withAnimation(.spring(response: 0.35, dampingFraction: 0.8)) {
            index = (index + d + tracks.count) % max(1, tracks.count)
        }
    }
}

// ---------------------------------------------------------------- poster art

/// A miniature of the scene the circuit sits in, for the inside of the poster
/// card. It is the `Backdrop` composition without the animation or the road:
/// sky, sun, two ridges and the ground - enough to read the place in a card
/// the size of a playing card.
struct PosterScene: View {
    var palette: ScenePalette

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let horizon = h * 0.42
            ZStack {
                LinearGradient(
                    stops: [.init(color: palette.zenith, location: 0),
                            .init(color: palette.high, location: 0.24),
                            .init(color: palette.horizon, location: 0.415),
                            .init(color: palette.ground.lighter(0.06),
                                  location: 0.425),
                            .init(color: palette.ground.darker(0.10),
                                  location: 1)],
                    startPoint: .top, endPoint: .bottom)

                Circle()
                    .fill(RadialGradient(
                        colors: [palette.sun, palette.sunGlow.opacity(0.5),
                                 .clear],
                        center: .center, startRadius: 0, endRadius: h * 0.20))
                    .frame(width: h * 0.40, height: h * 0.40)
                    .position(x: w * palette.sunAt.x, y: horizon - h * 0.16)
                    .blendMode(.plusLighter)

                ridge(peak: h * 0.13, freq: 1.6, phase: 1.1,
                      color: palette.hillFar, nearness: 0.2,
                      w: w, h: h, horizon: horizon)
                ridge(peak: h * 0.07, freq: 1.0, phase: 3.4,
                      color: palette.hillMid, nearness: 0.6,
                      w: w, h: h, horizon: horizon)

                scenery(w: w, h: h, horizon: horizon)

                Grain(opacity: 0.06)
                Vignette(strength: 0.30, power: 0.5)
            }
        }
    }

    /// The same trees and towers as the full backdrop, at poster scale, so a
    /// card is recognisable as the place it depicts.
    @ViewBuilder
    private func scenery(w: CGFloat, h: CGFloat,
                         horizon: CGFloat) -> some View {
        let kind: SceneryRow.Kind? = {
            switch palette.scenery {
            case .palms: return .palms
            case .pines: return .pines
            case .broadleaf: return .broadleaf
            case .none: return nil
            }
        }()
        if let kind {
            let bandH = h * 0.14
            SceneryRow(kind: kind,
                       tint: palette.hillMid.darker(0.18), seed: 17)
                .frame(width: w, height: bandH)
                .position(x: w / 2, y: horizon - bandH / 2)
                .opacity(0.85)
        }
    }

    private func ridge(peak: CGFloat, freq: CGFloat, phase: CGFloat,
                       color: Color, nearness: Double,
                       w: CGFloat, h: CGFloat, horizon: CGFloat) -> some View {
        let bandH = h - horizon + peak
        let amp = peak / (1.38 * bandH)
        return Ridge(amp: amp, freq: freq, phase: phase, base: peak / bandH)
            .fill(Ridge.haze(color, palette.horizon, nearness))
            .frame(width: w, height: bandH)
            .position(x: w / 2, y: horizon - peak + bandH / 2)
    }
}
