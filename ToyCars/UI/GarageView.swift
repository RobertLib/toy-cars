//
//  GarageView.swift
//  ToyCars
//
//  The showroom. One car at a time, lit on a turntable in a dark studio, with
//  its numbers on a card beside it - the layout every car game has settled on,
//  because it lets the player compare two things at once: how the car looks and
//  what it will do.
//
//  Three things were added over a plain list of stats:
//
//  * a class shield (D…S) worked out from the stats, so a car can be judged at
//    a glance before any bar is read;
//  * segmented meters rather than smooth bars - countable, and the top two
//    segments run hotter so a standout stat announces itself;
//  * a strip of every car along the bottom. Stepping through eight cars with
//    two arrows hides the collection; showing all eight makes it a collection.
//
//  Layout follows the same budget as track selection - a header, a capped
//  band, a strip - and takes its gaps and sizes from `Metrics`, so the two
//  screens are the same shape at every screen size. The card carries a lot
//  (a shield, four meters, two figures and a button), so it is grouped by
//  spacing rather than ruled off twice: one hairline under the header, then
//  gaps that say what belongs with what.
//
//  It carries *nearly* too much. Measured in landscape, the view gets 355
//  points of height on a 12/13 mini, 375 on an SE and 382 on an iPhone 17,
//  and the card's own content comes to about 245 - which is more than the
//  first two have left once the header, the strip and the margins have
//  taken their share, so the car strip ran off the bottom of both. The rule
//  this project's screens follow is to take something out rather than close
//  the gaps up, and the same rule the title screen uses for its hero car
//  decides when: below `figuresFit` the two hard figures are **dropped, not
//  shrunk**. They are the detail tier - the meters above them already rank
//  every car, and the shield ranks the car outright - so they are what a
//  screen with no room to spare can do without.
//

import SwiftUI

// ---------------------------------------------------------------- screen

struct GarageView: View {
    @Bindable var app: AppModel
    @State private var index: Int = 0
    private let cars = CarCatalog.shared.cars

    /// The shortest landscape view that still has room for the two hard
    /// figures at the foot of the card. Measured, not guessed: a 12/13 mini
    /// hands this view 355 points, an SE 375 and an iPhone 17 382, and only
    /// the last of those fits the card whole.
    private static let figuresFit: CGFloat = 380

    private var car: CarDef? { cars.indices.contains(index) ? cars[index] : nil }
    private var owned: Bool { car.map { app.progress.owns($0.id) } ?? false }
    /// Owned as well as selected. `GameProgress` guarantees the two agree, so
    /// this is belt and braces - but the badge and the price tag are drawn
    /// from different conditions and the one thing they must never do is
    /// appear together, which is what "in use" over "buy for 350" looked
    /// like on a profile where they had come apart.
    private var isSelected: Bool {
        guard let car, owned else { return false }
        return app.progress.state.selectedCar == car.id
    }

    var body: some View {
        ZStack {
            ShowroomBackdrop(accent: car?.color ?? TC.tangerine)

            if let car {
                GeometryReader { geo in
                    let m = Metrics(geo.size)
                    VStack(spacing: 0) {
                        header(m)
                            .frame(height: m.pt(42))
                        Spacer(minLength: m.tight)
                        // Capped on a tall screen: a card stretched to a
                        // tablet's full height is mostly empty cream with a
                        // button at the bottom. The slack becomes margin.
                        HStack(alignment: .top, spacing: m.gutter) {
                            stage(car, m)
                            details(car, m,
                                    figures: geo.size.height
                                        >= Self.figuresFit)
                                .frame(width: m.sidePanel)
                        }
                        .frame(maxHeight: m.pt(300))
                        Spacer(minLength: m.item)
                        carStrip(m)
                    }
                    .padding(.horizontal, m.edge)
                    .padding(.vertical, m.edgeV)
                }
            } else {
                Text("No cars found")
                    .font(TC.body(16)).foregroundStyle(.white)
            }
        }
        .onAppear {
            index = cars.firstIndex { $0.id == app.progress.state.selectedCar }
                ?? 0
        }
        .gesture(
            DragGesture(minimumDistance: 26)
                .onEnded { v in
                    if v.translation.width < -24 { move(1) }
                    if v.translation.width > 24 { move(-1) }
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
            RibbonBanner(text: String(localized: "GARAGE"), color: TC.tangerine,
                         size: m.pt(19))
            Spacer()
            CoinPill(coins: app.progress.state.coins, size: m.pt(20))
        }
    }

    // ------------------------------------------------------------ stage

    /// The car itself. The 3D view is transparent, so the studio lighting -
    /// the pool on the floor, the coloured haze behind the roofline - is
    /// painted around it here and reads as one continuous room.
    private func stage(_ car: CarDef, _ m: Metrics) -> some View {
        GeometryReader { g in
            // The preview camera has a fixed field of view, so the car grows
            // with the view it is drawn in. Capped, or on a tablet the model
            // fills the stage and its nose runs off the edge.
            let pw = min(g.size.width, 640)
            let ph = min(g.size.height, 440)
            // The studio lighting is measured off that same box rather than
            // off `Metrics` or, as it used to be, off nothing at all. These
            // were the last fixed point sizes on the screen, so on a tablet
            // the haze and the floor reflection stayed the size they are on a
            // phone while the car they belong to grew past them - the pool of
            // light ended up smaller than the model standing in it. `pw` and
            // `ph` are what decide how big the car is drawn, cap included, so
            // they are what the light has to follow.
            let hazeW = pw * 0.753
            let hazeH = ph * 0.828
            ZStack {
            // Coloured haze behind the car, in its own paint.
            Ellipse()
                .fill(RadialGradient(
                    colors: [car.color.opacity(0.42),
                             car.color.opacity(0.10), .clear],
                    center: .center, startRadius: 0,
                    endRadius: hazeW * 0.476))
                .frame(width: hazeW, height: hazeH)
                .offset(y: -ph * 0.057)
                .blendMode(.plusLighter)

            CarPreviewView(carID: car.id, accent: car.color,
                           spinSpeed: 0.5, studio: .garage)
                .frame(width: pw, height: ph)
                .opacity(owned ? 1 : 0.55)
                .saturation(owned ? 1 : 0.35)

            // The turntable's own reflection, thrown forwards on the floor.
            Ellipse()
                .fill(LinearGradient(
                    colors: [car.color.opacity(0.22), .clear],
                    startPoint: .top, endPoint: .bottom))
                .frame(width: pw * 0.448, height: ph * 0.191)
                .blur(radius: ph * 0.038)
                .offset(y: ph * 0.344)
                .blendMode(.plusLighter)

            if !owned { lockedPlate(car, m) }

            HStack {
                arrow("chevron.left", m, "Previous car") { move(-1) }
                Spacer()
                arrow("chevron.right", m, "Next car") { move(1) }
            }

            VStack {
                Spacer()
                nameBoard(car, m)
            }
            }
            .frame(width: g.size.width, height: g.size.height)
        }
    }

    private func selectedFlag(_ m: Metrics) -> some View {
        HStack(spacing: m.tight) {
            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: m.pt(11), weight: .black))
            Text("IN USE").font(TC.label(m.pt(10)))
        }
        .foregroundStyle(TC.ink)
        .padding(.horizontal, m.pt(10)).padding(.vertical, m.pt(5))
        .background(Capsule().fill(TC.metalGradient(TC.gold)))
        .shadow(color: TC.gold.dark.opacity(0.5), radius: 4, y: 2)
    }

    /// The plate under the car, as a museum label: name, the badge saying this
    /// is the car being raced, then the tagline. The badge belongs here rather
    /// than in a corner - it is a fact about this car, so it reads with the
    /// name.
    private func nameBoard(_ car: CarDef, _ m: Metrics) -> some View {
        VStack(spacing: m.hair) {
            HStack(spacing: m.snug) {
                StrokeText(text: Text(verbatim: car.name.uppercased())
                    .font(TC.title(m.pt(30))),
                           width: m.pt(2.4), color: TC.ink,
                           dropShadow: m.pt(2.5))
                    .foregroundStyle(TC.metalGradient(TC.silver))
                if isSelected { selectedFlag(m) }
            }
            Text(car.localizedTagline)
                .font(TC.body(m.pt(11), .bold))
                .foregroundStyle(.white.opacity(0.7))
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
    }

    private func lockedPlate(_ car: CarDef, _ m: Metrics) -> some View {
        VStack(spacing: m.snug) {
            ZStack {
                Circle().fill(TC.ink.opacity(0.7))
                Circle().strokeBorder(TC.sun.opacity(0.8), lineWidth: 2)
                Image(systemName: "lock.fill")
                    .font(.system(size: m.pt(22), weight: .black))
                    .foregroundStyle(TC.sun)
            }
            .frame(width: m.pt(54), height: m.pt(54))
            PriceTag(price: car.price,
                     affordable: app.progress.state.coins >= car.price)
        }
        .offset(y: -m.pt(30))
    }

    private func arrow(_ icon: String, _ m: Metrics,
                       _ label: LocalizedStringKey,
                       _ action: @escaping () -> Void) -> some View {
        Button(action: { Haptics.tap(); GameAudio.shared.uiTap(); action() }) {
            Image(systemName: icon)
        }
        .buttonStyle(CircleIconButtonStyle(color: TC.cream, size: m.btnIcon))
        .accessibilityLabel(label)
    }

    // ------------------------------------------------------------ details

    private func details(_ car: CarDef, _ m: Metrics,
                         figures: Bool) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            // -- identity
            HStack(alignment: .center, spacing: m.snug) {
                // The class shield first: it is the summary the rest of the
                // panel then explains.
                ClassBadge(letter: car.classLetter, color: car.classColor,
                           size: m.pt(30))
                VStack(alignment: .leading, spacing: m.hair * 0.5) {
                    Text("PERFORMANCE")
                        .font(TC.label(m.pt(10)))
                        .foregroundStyle(TC.inkSoft)
                    StarRating(earned: car.stars, size: m.pt(15))
                }
                Spacer(minLength: 0)
                // Paint chip - the colour, as a moulded plastic swatch.
                ZStack {
                    Circle().fill(TC.plasticGradient(car.color))
                    Gloss(shape: Circle(), strength: 0.55, extent: 0.5)
                    Circle().strokeBorder(.white, lineWidth: 2)
                }
                .frame(width: m.pt(26), height: m.pt(26))
                .shadow(color: TC.contact, radius: 3, y: 2)
            }

            Rectangle().fill(TC.ink.opacity(0.08)).frame(height: 1)
                .padding(.vertical, m.snug)

            // -- what it will do
            VStack(spacing: m.tight * 1.5) {
                SegmentMeter(label: "SPEED", value: car.speedScore,
                             color: TC.cherry, icon: "speedometer",
                             scale: m.type)
                SegmentMeter(label: "ACCELERATION", value: car.accelScore,
                             color: TC.tangerine, icon: "bolt.fill",
                             scale: m.type)
                SegmentMeter(label: "GRIP", value: car.gripScore,
                             color: TC.mint, icon: "circle.dashed",
                             scale: m.type)
                SegmentMeter(label: "FUEL TANK", value: car.fuelScore,
                             color: TC.sky, icon: "fuelpump.fill",
                             scale: m.type)
            }

            // -- the two hard figures. Grouped by a gap rather than a second
            // hairline: one rule on a card reads as structure, two read as a
            // form. Dropped outright on a screen with no room for them -
            // see `figuresFit`.
            if figures {
                HStack(spacing: m.section) {
                    spec("WEIGHT", String(format: "%.0f kg", car.mass), m)
                    spec("TOP", "\(car.displayTopSpeed) km/h", m)
                }
                .padding(.top, m.item)
            }

            Spacer(minLength: m.snug)
            actionButton(car, m)
        }
        .padding(.horizontal, m.pad)
        .padding(.vertical, m.snug * 1.6)
        .frame(maxHeight: .infinity)
        .background(PanelSurface(fill: TC.cream, corner: m.rCard))
    }

    private func spec(_ k: LocalizedStringKey, _ v: String,
                      _ m: Metrics) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(k).font(TC.label(m.pt(9))).foregroundStyle(TC.inkSoft)
            Text(verbatim: v).font(TC.readout(m.pt(14)))
                .foregroundStyle(TC.ink)
        }
    }

    @ViewBuilder
    private func actionButton(_ car: CarDef, _ m: Metrics) -> some View {
        let h = m.pt(47)
        if owned {
            if isSelected {
                Button {
                    Haptics.tap(); GameAudio.shared.uiTap(); app.go(.tracks)
                } label: {
                    ActionLabel(title: "RACE THIS ONE",
                                icon: "flag.checkered", size: h)
                }
                .buttonStyle(ChunkyButtonStyle(color: TC.lime, height: h,
                                               sheen: true))
            } else {
                Button {
                    Haptics.success(); GameAudio.shared.pickup()
                    app.progress.select(car.id)
                } label: {
                    ActionLabel(title: "SELECT",
                                icon: "checkmark.circle.fill",
                                showChevron: false, size: h)
                }
                .buttonStyle(ChunkyButtonStyle(color: TC.sun, height: h))
            }
        } else {
            let can = app.progress.canBuy(car)
            // Not `.disabled(!can)`, which is what this was: the refusal
            // branch below could then never run, so a player who tapped
            // "NEED 250 MORE" got no press, no haptic and no sound at all -
            // a button that reads as broken rather than as withheld. It stays
            // pressable and says no, which is what the plate already says in
            // words; `accessibilityHint` carries the same for VoiceOver.
            Button {
                if can {
                    Haptics.success(); GameAudio.shared.lap()
                    app.progress.buy(car)
                    app.progress.select(car.id)
                } else {
                    Haptics.warn(); GameAudio.shared.uiTap()
                }
            } label: {
                ActionLabel(
                    title: can ? "BUY FOR \(car.price)"
                               : "NEED \(car.price - app.progress.state.coins) MORE",
                    icon: can ? "cart.fill" : "lock.fill",
                    tint: .white, showChevron: false, size: h)
            }
            // `Text(verbatim:)` for the empty case: a "" hint would otherwise
            // be looked up in the string catalogue as a key of its own.
            .accessibilityHint(can ? Text(verbatim: "")
                                   : Text("Not enough coins yet"))
            .buttonStyle(ChunkyButtonStyle(
                color: can ? TC.grape : TC.steel.darker(0.10),
                textColor: .white, height: h))
        }
    }

    // ------------------------------------------------------------ strip

    /// Every car in the game, always visible: owned ones in their paint,
    /// locked ones as grey blanks with a padlock.
    private func carStrip(_ m: Metrics) -> some View {
        HStack(spacing: m.snug * 1.4) {
            ForEach(Array(cars.enumerated()), id: \.element.id) { i, c in
                ChipButton(color: c.color,
                           label: c.name.uppercased(),
                           selected: i == index,
                           locked: !app.progress.owns(c.id),
                           scale: m.type) {
                    Haptics.tap(); GameAudio.shared.uiTap()
                    withAnimation(.spring(response: 0.34,
                                          dampingFraction: 0.78)) {
                        index = i
                    }
                }
            }
        }
        .padding(.horizontal, m.snug * 1.4)
        .padding(.vertical, m.tight * 1.6)
        .background(GlassSurface(corner: m.rPanel, opacity: 0.34))
    }

    private func move(_ d: Int) {
        Haptics.tap(); GameAudio.shared.uiTap()
        withAnimation(.spring(response: 0.35, dampingFraction: 0.8)) {
            index = (index + d + cars.count) % max(1, cars.count)
        }
    }
}
