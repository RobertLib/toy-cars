//
//  ResultsView.swift
//  ToyCars
//
//  The reward screen. It is the last thing a player sees before deciding
//  whether to go again, so it is built to pay out rather than to report:
//
//  * the medal is struck metal and lands with a bounce;
//  * the placing is spelled out beside it, because the medal carries the
//    feeling and the words carry the fact;
//  * the stars come in one at a time, each with a click;
//  * the coins count up rather than appearing, because a number that moves
//    feels earned;
//  * a new best lap and a newly unlocked track get their own ribbons - the two
//    things worth shouting about.
//
//  A race that ended badly gets the same care in the other direction: grey
//  metal, no confetti, and the buttons still one tap from another go.
//
//  The composition is three blocks with unmistakable space between them -
//  what happened, what it paid, what to do next - and the standings beside
//  them. That separation is the whole layout: every figure on this screen is
//  a number on a cream tile, so if the gaps between the blocks are no wider
//  than the gaps inside them, the column reads as one undifferentiated mass
//  and none of it is legible at a glance. Gaps come from `Metrics`, where
//  `group` is guaranteed to be visibly wider than `item`.
//

import SwiftUI

struct ResultsView: View {
    @Bindable var app: AppModel
    @State private var appear = false
    @State private var shownCoins = 0

    private var outcome: GameProgress.RaceOutcome? { app.lastOutcome }
    private var position: Int { outcome?.position ?? 0 }
    private var finished: Bool { outcome?.finished ?? false }
    private var medal: TC.Metal? {
        finished ? TC.medal(for: position) : nil
    }
    /// Three stars for a win, two for the podium, one for finishing.
    private var stars: Int {
        guard finished else { return 0 }
        switch position {
        case 1: return 3
        case 2, 3: return 2
        default: return 1
        }
    }

    /// A podium finish is celebrated in the colour of the metal earned.
    /// Finishing off the podium is not a failure, so it keeps the circuit's
    /// own evening light; only a retirement gets the grey sky.
    private var palette: ScenePalette {
        if medal != nil { return ScenePalette.podium(medal) }
        if finished {
            return ScenePalette.forTheme(
                TrackCatalog.shared.track(app.lastTrackID)?.theme)
        }
        return ScenePalette.podium(nil)
    }

    var body: some View {
        ZStack {
            Backdrop(palette: palette, showRoad: false, vignette: 0.5)

            if finished, position <= 3 {
                ConfettiView(count: position == 1 ? 110 : 70)
            }

            GeometryReader { geo in
                let m = Metrics(geo.size)
                // The height the two columns share, once the screen margins
                // are taken off. The standings size their rows to it, so the
                // panel ends up filling its side exactly rather than leaving
                // a pool of empty cream under the last finisher.
                let band = geo.size.height - m.edgeV * 2
                HStack(alignment: .center, spacing: m.gutter) {
                    report(m)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                    standings(m, band: band)
                        .frame(width: m.sidePanel)
                }
                .padding(.horizontal, m.edge)
                .padding(.vertical, m.edgeV)
            }
        }
        .onAppear {
            withAnimation(.spring(response: 0.55, dampingFraction: 0.6)) {
                appear = true
            }
            if finished { Haptics.success() }
        }
        .task { await countCoins() }
    }

    // ------------------------------------------------------------ report

    /// The left column: verdict, payout, actions. The actions are pushed to
    /// the bottom by a flexible spacer rather than sitting wherever the
    /// content above them happens to end - a screen whose main button lands
    /// at a different height every time reads as unfinished, and the bottom
    /// of the screen is where a thumb already is.
    private func report(_ m: Metrics) -> some View {
        VStack(spacing: 0) {
            verdict(m)
            payout(m)
                .padding(.top, m.group)
            Spacer(minLength: m.item)
            actions(m)
        }
        // Capped in both directions: a row of controls stretched across a
        // tablet reads as a banner rather than as something to press, and a
        // column given a tablet's full height puts the medal at the very top
        // of the glass and the buttons at the very bottom with a hole in
        // between. Neither cap binds on a phone.
        .frame(maxWidth: m.column * 1.24, maxHeight: m.pt(370))
    }

    // ------------------------------------------------------------ verdict

    /// Medal and headline side by side. Landscape is short of height - a
    /// phone gives this column about 350 points for all three blocks - so the
    /// hero is laid out across the screen rather than down it. Stacking the
    /// medal above the headline, which is what a victory screen wants to do,
    /// costs a hundred of those points and pushes the buttons off the glass.
    private func verdict(_ m: Metrics) -> some View {
        HStack(alignment: .center, spacing: m.item) {
            trophy(m)

            VStack(alignment: .leading, spacing: m.tight) {
                StrokeText(text: Text(headline).font(TC.title(m.pt(31))),
                           width: m.pt(2.6), color: TC.ink,
                           dropShadow: m.pt(3.6))
                    .foregroundStyle(TC.metalGradient(medal ?? TC.silver))
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)

                if finished {
                    // Fact and grade on one line, divided by a rule - the
                    // same pairing the car name plate uses on the title
                    // screen, so the two screens rhyme.
                    HStack(spacing: m.snug) {
                        Text("Position \(position) of \(app.lastFinalOrder.count)")
                            .font(TC.body(m.pt(12), .black))
                            .foregroundStyle(.white.opacity(0.92))
                            .lineLimit(1)
                        if stars > 0 {
                            Rectangle().fill(.white.opacity(0.35))
                                .frame(width: 1, height: m.pt(14))
                            StarRating(earned: stars, size: m.pt(17),
                                       animated: true)
                        }
                    }
                }
            }
            Spacer(minLength: 0)
        }
        // The medal's ribbon tails hang below its disc without taking layout
        // space, so the block is given the room back here.
        .padding(.bottom, medal != nil ? m.tight : 0)
        .scaleEffect(appear ? 1 : 0.88)
    }

    /// What the player brought home: a medal for the podium, a chrome plate
    /// with the placing for everyone who finished, a struck-through flag for
    /// a retirement.
    @ViewBuilder
    private func trophy(_ m: Metrics) -> some View {
        if let mt = medal {
            MedalBadge(position: position, size: m.pt(70))
                .scaleEffect(appear ? 1 : 0.4)
                .rotationEffect(.degrees(appear ? 0 : -25))
                .shadow(color: mt.body.opacity(0.6), radius: m.pt(16))
        } else if finished {
            // Fourth or worse: still a result, so it gets a plate with the
            // placing on it rather than a crossed-out flag.
            ZStack {
                RoundedRectangle(cornerRadius: m.pt(15), style: .continuous)
                    .fill(TC.metalGradient(TC.chrome))
                RoundedRectangle(cornerRadius: m.pt(15), style: .continuous)
                    .strokeBorder(TC.chrome.dark.opacity(0.8),
                                  lineWidth: m.pt(1.5))
                HStack(alignment: .firstTextBaseline, spacing: 0) {
                    Text(verbatim: "\(position)")
                        .font(TC.readout(m.pt(30)))
                    Text(verbatim: ordinalSuffix(position))
                        .font(TC.title(m.pt(13)))
                        .offset(y: -m.pt(2))
                }
                .foregroundStyle(TC.ink)
            }
            .frame(width: m.pt(68), height: m.pt(58))
            .scaleEffect(appear ? 1 : 0.5)
            .lift(m.pt(9))
        } else {
            Image(systemName: "flag.slash.fill")
                .font(.system(size: m.pt(40), weight: .black))
                .foregroundStyle(.white.opacity(0.72))
                .frame(width: m.pt(68))
                .shadow(color: TC.ink.opacity(0.5), radius: m.pt(6), y: 2)
        }
    }

    private var headline: LocalizedStringKey {
        guard let o = outcome else { return "RACE OVER" }
        if !o.finished { return "DID NOT FINISH" }
        switch o.position {
        case 1: return "VICTORY!"
        case 2, 3: return "ON THE PODIUM!"
        default: return "FINISHED"
        }
    }

    // ------------------------------------------------------------ payout

    /// The three figures the race produced, then the announcements. The
    /// figures are the second-loudest thing on the screen after the headline,
    /// because they are what the player came for; the old layout set them in
    /// fifteen points, smaller than the times in the standings panel beside
    /// them, which put the emphasis on the wrong column entirely.
    @ViewBuilder
    private func payout(_ m: Metrics) -> some View {
        if let o = outcome {
            VStack(alignment: .leading, spacing: m.item) {
                HStack(spacing: m.snug) {
                    StatTile(label: "Time",
                             value: o.finished ? formatTime(o.totalTime) : "—",
                             icon: "stopwatch.fill",
                             scale: m.type)
                    StatTile(label: "Best lap",
                             value: o.bestLap.isFinite
                                 ? formatTime(o.bestLap) : "—",
                             icon: "timer",
                             rim: o.newBestLap ? TC.teal : nil,
                             valueTint: o.newBestLap ? TC.teal : nil,
                             scale: m.type)
                    StatTile(label: "Coins", value: "+\(shownCoins)",
                             coin: true, rim: TC.sun,
                             scale: m.type)
                }

                // Stacked rather than shouldered together on one line: the
                // two ribbons are different lengths, and a row of two
                // different-length capsules stretched to fit reads as a
                // mistake. Each one hugs its own text and starts at the
                // column's left edge.
                if o.newBestLap || o.unlockedTrack != nil {
                    VStack(alignment: .leading, spacing: m.snug) {
                        if o.newBestLap {
                            ribbon("NEW BEST LAP!", color: TC.mint,
                                   icon: "bolt.fill", m: m)
                        }
                        if let t = o.unlockedTrack {
                            ribbon("\(t.name) unlocked!", color: TC.sun,
                                   icon: "lock.open.fill", m: m)
                        }
                    }
                }
            }
        }
    }

    private func ribbon(_ text: LocalizedStringKey, color: Color,
                        icon: String, m: Metrics) -> some View {
        HStack(spacing: m.tight * 1.5) {
            Image(systemName: icon)
                .font(.system(size: m.pt(11), weight: .black))
            Text(text).font(TC.body(m.pt(12), .black))
                .lineLimit(1)
        }
        .foregroundStyle(TC.ink)
        .padding(.horizontal, m.pt(11)).padding(.vertical, m.pt(6))
        .background {
            ZStack {
                Capsule().fill(TC.plasticGradient(color))
                Gloss(shape: Capsule(), strength: 0.4, extent: 0.5)
                Capsule().strokeBorder(.white.opacity(0.6), lineWidth: 1.2)
            }
            .compositingGroup()
            .shadow(color: TC.contact, radius: 4, y: 2)
        }
        .transition(.scale.combined(with: .opacity))
    }

    // ------------------------------------------------------------ actions

    /// One wide primary and a pair of halves under it. Three full-width slabs
    /// would carry the same three taps but read as a wall; one-plus-two says
    /// which of them is the obvious next move.
    private func actions(_ m: Metrics) -> some View {
        VStack(spacing: m.snug) {
            Button {
                Haptics.tap(); GameAudio.shared.uiTap()
                if let t = TrackCatalog.shared.track(app.lastTrackID) {
                    app.startRace(track: t)
                }
            } label: {
                ActionLabel(title: "RACE AGAIN", icon: "arrow.clockwise",
                            showChevron: false, size: m.btnPrimary)
            }
            .buttonStyle(ChunkyButtonStyle(color: TC.lime, height: m.btnPrimary,
                                           sheen: true))

            HStack(spacing: m.snug) {
                Button {
                    Haptics.tap(); GameAudio.shared.uiTap(); app.go(.tracks)
                } label: {
                    ActionLabel(title: "TRACKS", icon: "map.fill",
                                showChevron: false, size: m.btnSecondary)
                }
                .buttonStyle(ChunkyButtonStyle(color: TC.sun,
                                               height: m.btnSecondary))
                Button {
                    Haptics.tap(); GameAudio.shared.uiTap(); app.go(.garage)
                } label: {
                    ActionLabel(title: "GARAGE", icon: "car.side.fill",
                                showChevron: false, size: m.btnSecondary)
                }
                .buttonStyle(ChunkyButtonStyle(color: TC.cream,
                                               height: m.btnSecondary))
            }
        }
    }

    // ------------------------------------------------------------ standings

    /// The finishing order. Rows are sized to divide the height the panel has
    /// rather than to a fixed point value, so the list fills its card on a
    /// phone and on a tablet alike and never has to scroll for a normal
    /// field. The clamp keeps a small field from producing absurdly tall rows
    /// and a huge one from producing unreadable ones.
    private func standings(_ m: Metrics, band: CGFloat) -> some View {
        let n = max(1, app.lastFinalOrder.count)
        let headH = m.pt(34)
        let footer = m.snug
        let rowH = max(m.pt(30),
                       min(m.pt(46), (band - headH - footer) / CGFloat(n)))
        return VStack(spacing: 0) {
            HStack(spacing: m.tight) {
                Image(systemName: "list.number")
                    .font(.system(size: m.pt(10), weight: .black))
                Text("RESULTS").font(TC.label(m.pt(11)))
                Spacer()
                Text("TIME").font(TC.label(m.pt(9)))
            }
            .foregroundStyle(TC.inkSoft)
            .padding(.horizontal, m.pad)
            .frame(height: headH)

            Rectangle().fill(TC.ink.opacity(0.09))
                .frame(height: 1)
                .padding(.horizontal, m.pad * 0.6)

            ForEach(Array(app.lastFinalOrder.enumerated()),
                    id: \.offset) { i, r in
                standingRow(i: i, r: r, m: m, h: rowH)
            }
        }
        .padding(.bottom, footer)
        // The rows are full bleed - a striped row and the bright plate under
        // the player's own reach both edges of the card on purpose - so they
        // have to be masked to it. `footer` is a `snug` and the corner is an
        // `rCard`, three times wider, so the last row sits *inside* the
        // corner's arc: without this its background squares off the card's
        // bottom corners and the player's gold edge marker hangs in the
        // shadow beside it. Which is exactly the row a player who finished
        // last is looking at.
        .clipShape(RoundedRectangle(cornerRadius: m.rCard, style: .continuous))
        // After the clip, so the panel keeps its contact and ambient shadows.
        .background(PanelSurface(fill: TC.cream, corner: m.rCard))
    }

    private func standingRow(i: Int, r: RaceEngine.Standing,
                             m: Metrics, h: CGFloat) -> some View {
        let place = i + 1
        let disc = m.pt(23)
        return HStack(spacing: m.snug) {
            // The top three get their metal; the rest a plain number.
            if let mt = TC.medal(for: place) {
                ZStack {
                    Circle().fill(TC.metalGradient(mt))
                    Circle().strokeBorder(mt.dark.opacity(0.7), lineWidth: 1)
                    Text(verbatim: "\(place)")
                        .font(TC.readout(m.pt(12)))
                        .foregroundStyle(TC.ink.opacity(0.8))
                }
                .frame(width: disc, height: disc)
            } else {
                Text(verbatim: "\(place)")
                    .font(TC.readout(m.pt(13)))
                    .foregroundStyle(TC.inkSoft)
                    .frame(width: disc)
            }

            ZStack {
                Circle().fill(TC.plasticGradient(Color(hex: r.color)))
                Circle().strokeBorder(.white, lineWidth: 1.5)
            }
            .frame(width: m.pt(15), height: m.pt(15))

            Text(verbatim: r.name)
                .font(TC.body(m.pt(14), r.isPlayer ? .black : .bold))
                .foregroundStyle(TC.ink)
                .lineLimit(1)

            if r.isPlayer {
                Text("YOU")
                    .font(TC.label(m.pt(8)))
                    .foregroundStyle(.white)
                    .padding(.horizontal, m.pt(5))
                    .padding(.vertical, m.pt(2))
                    .background(Capsule().fill(TC.cherry))
            }
            Spacer(minLength: m.tight)
            Text(verbatim: r.finished ? formatTime(r.time) : "DNF")
                .font(TC.readout(m.pt(12), .bold))
                .foregroundStyle(r.finished ? TC.inkSoft : TC.cherry)
        }
        .frame(height: h)
        .padding(.horizontal, m.pad)
        .background {
            if r.isPlayer {
                LinearGradient(colors: [TC.sun.opacity(0.35),
                                        TC.sun.opacity(0.12)],
                               startPoint: .leading, endPoint: .trailing)
                    .overlay(alignment: .leading) {
                        Rectangle().fill(TC.sun).frame(width: 3)
                    }
            } else if i % 2 == 0 {
                Color.black.opacity(0.035)
            }
        }
    }

    // ------------------------------------------------------------ payout

    /// Counts the reward up over about half a second, with a tick every few
    /// steps. A number that moves feels earned; one that is simply there does
    /// not.
    private func countCoins() async {
        guard let total = outcome?.coins, total > 0 else { return }
        let steps = min(24, total)
        try? await Task.sleep(for: .milliseconds(350))
        for i in 1...steps {
            shownCoins = total * i / steps
            if i % 4 == 0 { Haptics.tap() }
            try? await Task.sleep(for: .milliseconds(22))
        }
    }
}
