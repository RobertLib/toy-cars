//
//  MainMenuView.swift
//  ToyCars
//
//  The title screen. It has one job beyond starting a race: to say what kind
//  of game this is within a second of the app opening. So the player's own car
//  is standing right there on a turntable, lit, turning, with its name on a
//  plate under it - the same shot a toy gets on the front of its box.
//
//  The game runs in landscape, so the layout is two columns: the badge and the
//  buttons down the left where a thumb reaches, the car filling the right.
//  On a narrow phone the car is dropped rather than shrunk, because a small
//  car is worse than no car.
//
//  The left column is three things at fixed stations - badge at the top,
//  actions above the bottom, footprint under them - with all the slack put
//  into the one gap between the badge and the actions. Three full-width
//  buttons stacked down the lower half, which is what this screen used to
//  have, carry the same three taps but read as a wall of plastic: they crowd
//  the car, they leave no ground under them, and none of them looks more
//  pressable than the others. One wide button over a pair of halves says
//  which is the obvious next move and gives back a third of the height.
//

import SwiftUI

struct MainMenuView: View {
    @Bindable var app: AppModel
    @State private var bob: CGFloat = 0
    @State private var enter = false
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// Read from the bundle rather than written into the string: a version
    /// baked into a localisable literal has to be re-translated to bump.
    private static let version = Bundle.main.object(
        forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""

    private var car: CarDef? {
        CarCatalog.shared.car(app.progress.state.selectedCar)
    }

    var body: some View {
        GeometryReader { geo in
            let m = Metrics(geo.size)
            let wide = geo.size.width > 700
            ZStack {
                Backdrop(palette: .menu, showRoad: true, vignette: 0.46)

                if wide {
                    hero(m, size: geo.size)
                }

                HStack(alignment: .center, spacing: 0) {
                    leftColumn(m, size: geo.size)
                        // Capped rather than taken as a share of the width:
                        // past about 340 points a button stops reading as a
                        // button and starts reading as a banner. The height
                        // cap keeps a tablet from stretching the badge and
                        // the buttons to opposite ends of the glass; it does
                        // not bind on a phone.
                        .frame(width: wide
                               ? min(m.column, geo.size.width * 0.42)
                               : geo.size.width - m.edge * 2,
                               alignment: .leading)
                        .frame(maxHeight: m.pt(400))
                    Spacer(minLength: 0)
                }
                .padding(.horizontal, m.edge)
                .padding(.vertical, m.edgeV)

                topBar(m)
                    .padding(.horizontal, m.edge)
                    .padding(.top, m.edgeV)
            }
        }
        .overlay {
            if app.showSettings {
                SettingsPanel(app: app)
                    .transition(.opacity.combined(with: .scale(scale: 0.94)))
            }
        }
        .onAppear(perform: onAppear)
    }

    // ------------------------------------------------------------ left

    /// Everything here is sized from the screen rather than fixed: a phone in
    /// landscape has about 400 points of height, an iPad more than twice that,
    /// and a badge that fits the phone looks lost on the tablet.
    ///
    /// One flexible gap, between the badge and the buttons, takes all the
    /// height a screen has spare. Everything else is a fixed distance from its
    /// neighbour, so the buttons and the footprint keep their relationship to
    /// each other whatever the screen does - and the footprint stops being
    /// something jammed against the bottom edge of the glass.
    private func leftColumn(_ m: Metrics, size: CGSize) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            GameLogo(scale: min(m.pt(80), size.height * 0.19))
                .offset(y: bob)

            Spacer(minLength: m.section)

            buttons(m)
                .offset(x: enter ? 0 : -40)
                .opacity(enter ? 1 : 0)

            footprint(m)
                .padding(.top, m.group)
        }
    }

    /// One wide primary with a pair of halves under it.
    private func buttons(_ m: Metrics) -> some View {
        VStack(spacing: m.snug) {
            Button {
                Haptics.tap(); GameAudio.shared.uiTap()
                app.go(.tracks)
            } label: {
                ActionLabel(title: "RACE", icon: "flag.checkered",
                            size: m.btnPrimary)
            }
            .buttonStyle(ChunkyButtonStyle(color: TC.lime,
                                           height: m.btnPrimary,
                                           sheen: true))

            HStack(spacing: m.snug) {
                Button {
                    Haptics.tap(); GameAudio.shared.uiTap()
                    app.go(.garage)
                } label: {
                    ActionLabel(title: "GARAGE", icon: "car.side.fill",
                                showChevron: false, size: m.btnSecondary)
                }
                .buttonStyle(ChunkyButtonStyle(color: TC.tangerine,
                                               height: m.btnSecondary))

                Button {
                    Haptics.tap(); GameAudio.shared.uiTap()
                    withAnimation(.spring(response: 0.35,
                                          dampingFraction: 0.8)) {
                        app.showSettings = true
                    }
                } label: {
                    ActionLabel(title: "SETTINGS", icon: "gearshape.fill",
                                showChevron: false, size: m.btnSecondary)
                }
                .buttonStyle(ChunkyButtonStyle(color: TC.cream,
                                               textColor: TC.ink,
                                               height: m.btnSecondary))
            }
        }
    }

    private func footprint(_ m: Metrics) -> some View {
        HStack(spacing: m.tight * 1.6) {
            Image(systemName: "wifi.slash")
                .font(.system(size: m.pt(9), weight: .black))
            Text("v\(MainMenuView.version) · plays offline")
                .font(TC.body(m.pt(10), .bold))
        }
        .foregroundStyle(.white.opacity(0.6))
        .padding(.leading, m.tight)
    }

    // ------------------------------------------------------------ hero

    /// The car, composited straight onto the painted scene - the RealityView
    /// has no environment of its own, so it is transparent everywhere the car
    /// is not. It stands on the play-mat road rather than on a plinth, which
    /// is why the road's vanishing point is pushed right in `menuPalette`:
    /// the car is parked on the road the player is about to drive.
    private func hero(_ m: Metrics, size: CGSize) -> some View {
        // Sized and placed to sit in the gap between the button column and
        // the right edge: any wider and the car either runs under the buttons
        // or has its nose clipped off the screen.
        let w = min(size.width * 0.55, 700)
        let h = min(size.height * 0.80, 460)
        return VStack(spacing: 0) {
            ZStack {
                // Cast shadow. A directional light in the preview cannot land
                // a shadow on a road that only exists in 2D, so the contact
                // shadow is drawn here - squashed, offset away from the sun,
                // and darkest directly under the body.
                Ellipse()
                    .fill(RadialGradient(
                        colors: [TC.ink.opacity(0.72), TC.ink.opacity(0.34),
                                 .clear],
                        center: .center, startRadius: 0, endRadius: w * 0.24))
                    .frame(width: w * 0.52, height: h * 0.13)
                    .offset(x: -w * 0.03, y: h * 0.345)
                    .blur(radius: 6)

                // Warm bounce light off the tarmac.
                Ellipse()
                    .fill(RadialGradient(
                        colors: [TC.sun.opacity(0.22), .clear],
                        center: .center, startRadius: 0, endRadius: w * 0.30))
                    .frame(width: w * 0.86, height: h * 0.24)
                    .offset(y: h * 0.33)
                    .blendMode(.plusLighter)

                CarPreviewView(carID: app.progress.state.selectedCar,
                               accent: car?.color ?? TC.tangerine,
                               spinSpeed: 0.42,
                               studio: .sunset,
                               showTurntable: false)
                    .frame(width: w, height: h)
                    // The camera frames the car centrally, so the whole view
                    // is dropped to bring the wheels down onto the road.
                    .offset(y: h * 0.13 + bob * 0.4)
            }
            .frame(width: w, height: h)

            namePlate(m)
                .offset(y: -h * 0.02)
        }
        // Anchored to the bottom: the car's wheels have to meet the road, and
        // the road is where the ground is, not where the middle of the screen
        // happens to be.
        .frame(maxWidth: .infinity, maxHeight: .infinity,
               alignment: .bottom)
        .offset(x: size.width * 0.185, y: -size.height * 0.02)
        .opacity(enter ? 1 : 0)
        .allowsHitTesting(false)
    }

    /// On `Metrics`, like the rest of the screen. This was the last thing on
    /// the title screen still laid out in fixed points, so it was the one
    /// thing that stayed phone-sized while the logo, the buttons and the car
    /// beside it all grew - on a 13-inch tablet a 17-point name under a
    /// 460-point car, with "YOUR CAR" at nine points next to it.
    private func namePlate(_ m: Metrics) -> some View {
        HStack(spacing: m.snug) {
            Circle()
                .fill(TC.plasticGradient(car?.color ?? TC.tangerine))
                .frame(width: m.pt(14), height: m.pt(14))
                .overlay(Circle().strokeBorder(.white.opacity(0.85),
                                               lineWidth: m.pt(1.5)))
            Text(verbatim: car?.name.uppercased() ?? "")
                .font(TC.title(m.pt(17)))
                .foregroundStyle(TC.ink)
                .tracking(m.pt(1.2))
            Rectangle().fill(TC.ink.opacity(0.18))
                .frame(width: 1, height: m.pt(15))
            Text("YOUR CAR")
                .font(TC.label(m.pt(9)))
                .foregroundStyle(TC.inkSoft)
        }
        .padding(.horizontal, m.pad).padding(.vertical, m.tight * 2)
        .background {
            ZStack {
                Capsule().fill(TC.metalGradient(TC.silver))
                Sheen(shape: Capsule(), period: 5.8, strength: 0.4)
                Capsule().strokeBorder(TC.silver.dark.opacity(0.7),
                                       lineWidth: 1.2)
            }
            .compositingGroup()
            .lift(m.pt(8))
        }
    }

    // ------------------------------------------------------------ top bar

    private func topBar(_ m: Metrics) -> some View {
        VStack {
            HStack(alignment: .center, spacing: m.snug) {
                Spacer()
                TrophyRow(completed: TrackCatalog.shared.tracks.map {
                    app.progress.record($0.id).completed
                }, size: m.pt(15))
                .padding(.horizontal, m.pt(12))
                .padding(.vertical, m.pt(7))
                .background(GlassSurface(corner: m.rChip, opacity: 0.22))
                CoinPill(coins: app.progress.state.coins, size: m.pt(20))
            }
            Spacer()
        }
    }

    // ------------------------------------------------------------ lifecycle

    private func onAppear() {
        GameAudio.shared.enabled = app.progress.state.soundOn
        GameAudio.shared.musicEnabled = app.progress.state.musicOn
        Haptics.enabled = app.progress.state.hapticsEnabled
        GameAudio.shared.start()
        GameAudio.shared.setMusicContext(0.30)
        GameAudio.shared.silenceDriving()
        withAnimation(.spring(response: 0.7, dampingFraction: 0.8)) {
            enter = true
        }
        // The badge and the car breathe together for ever; under Reduce
        // Motion they simply sit still.
        guard !reduceMotion else { return }
        withAnimation(.easeInOut(duration: 3.0)
            .repeatForever(autoreverses: true)) {
            bob = -7
        }
    }
}

// ---------------------------------------------------------------- settings

/// Settings as an in-place panel rather than a system sheet. A sheet in
/// landscape covers the screen with a shape and a material that belong to iOS,
/// not to the game; this keeps the player inside the toybox.
struct SettingsPanel: View {
    @Bindable var app: AppModel

    var body: some View {
        ZStack {
            Color.black.opacity(0.5)
                .ignoresSafeArea()
                .onTapGesture { close() }

            GeometryReader { geo in
                let m = Metrics(geo.size)
                VStack(spacing: 0) {
                    header(m)
                    content(m)
                }
                .frame(maxWidth: m.column * 1.58)
                .background(PanelSurface(fill: TC.cream, corner: m.rPanel))
                .padding(.horizontal, m.edge)
                .padding(.vertical, m.edgeV)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }

    private func header(_ m: Metrics) -> some View {
        HStack {
            Image(systemName: "gearshape.fill")
                .font(.system(size: m.pt(17), weight: .black))
                .foregroundStyle(TC.grape)
            Text("SETTINGS")
                .font(TC.title(m.pt(20)))
                .foregroundStyle(TC.ink)
            Spacer()
            Button { close() } label: { Image(systemName: "xmark") }
                .buttonStyle(CircleIconButtonStyle(color: TC.cream,
                                                   size: m.btnIcon * 0.85))
                .accessibilityLabel("Close settings")
        }
        .padding(.horizontal, m.pad)
        .padding(.top, m.snug * 1.2)
        .padding(.bottom, m.snug)
    }

    private func content(_ m: Metrics) -> some View {
        // The height budget, and it is smaller than it looks. `Metrics.t`
        // bottoms out at a 380-point short edge, but the shortest phone this
        // runs on is a 12/13 mini at **360**, and once landscape's home
        // indicator is out that leaves about 310 points for the whole panel.
        // Four controls, their captions and a button did not fit: the panel
        // ran off the bottom of the screen and took the lower half of DONE
        // with it.
        //
        // The fix is the one this project's layout rules already call for -
        // take something out rather than close the gaps up, because
        // tightening this rhythm is what would make the panel feel packed.
        // What went was the line saying the game is offline and saves to the
        // device: the title screen behind this panel prints "plays offline"
        // in its own footprint, so it was the one row here that was telling
        // the player something they could already read.
        VStack(alignment: .leading, spacing: m.snug * 1.05) {
            Rectangle().fill(TC.ink.opacity(0.08)).frame(height: 1)

            row(icon: "speaker.wave.2.fill", tint: TC.mint, title: "Sound",
                m: m) {
                ToyToggle(isOn: app.progress.state.soundOn, tint: TC.mint) {
                    app.progress.setSound($0)
                    GameAudio.shared.enabled = $0
                }
            }
            row(icon: "music.note", tint: TC.grape, title: "Music", m: m) {
                ToyToggle(isOn: app.progress.state.musicOn, tint: TC.grape) {
                    app.progress.setMusic($0)
                    GameAudio.shared.musicEnabled = $0
                }
            }
            // Its own switch rather than riding on the sound one: a race
            // fires a haptic on every bump, canister and lap, which is a lot
            // of buzzing for a player who would rather keep the sound.
            row(icon: "iphone.radiowaves.left.and.right", tint: TC.tangerine,
                title: "Vibration", m: m) {
                ToyToggle(isOn: app.progress.state.hapticsEnabled,
                          tint: TC.tangerine) {
                    app.progress.setHaptics($0)
                }
            }

            VStack(alignment: .leading, spacing: m.tight * 1.4) {
                HStack(spacing: m.tight * 1.7) {
                    iconBadge("hand.point.up.left.fill", TC.sky, m)
                    Text("Controls")
                        .font(TC.body(m.pt(15), .black))
                        .foregroundStyle(TC.ink)
                }
                ToySegmented(options: ["Buttons", "Finger drag"],
                             selection: Binding(
                                get: { app.progress.state.steerMode },
                                set: { app.progress.setSteerMode($0) }),
                             tint: TC.sky)
                Text("The car handles the throttle itself. You take care of the corners, the brake and the drift.")
                    .font(TC.body(m.pt(11), .medium))
                    .foregroundStyle(TC.inkSoft)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Button("DONE") { close() }
                .buttonStyle(ChunkyButtonStyle(color: TC.sun,
                                               height: m.btnSecondary))
        }
        .padding(.horizontal, m.pad)
        .padding(.bottom, m.snug * 1.2)
        .padding(.top, m.hair)
    }

    private func row<C: View>(icon: String, tint: Color,
                              title: LocalizedStringKey, m: Metrics,
                              @ViewBuilder control: () -> C) -> some View {
        HStack(spacing: m.tight * 1.7) {
            iconBadge(icon, tint, m)
            Text(title)
                .font(TC.body(m.pt(15), .black))
                .foregroundStyle(TC.ink)
            Spacer()
            control()
        }
    }

    private func iconBadge(_ icon: String, _ tint: Color,
                           _ m: Metrics) -> some View {
        ZStack {
            Circle().fill(TC.plasticGradient(tint))
            Gloss(shape: Circle(), strength: 0.4, extent: 0.5)
            Image(systemName: icon)
                .font(.system(size: m.pt(13), weight: .black))
                .foregroundStyle(.white)
        }
        .frame(width: m.pt(30), height: m.pt(30))
        .shadow(color: TC.contact, radius: 2, y: 1)
    }

    private func close() {
        Haptics.tap(); GameAudio.shared.uiTap()
        withAnimation(.spring(response: 0.32, dampingFraction: 0.85)) {
            app.showSettings = false
        }
    }
}
