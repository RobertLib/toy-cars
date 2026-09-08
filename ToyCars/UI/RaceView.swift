//
//  RaceView.swift
//  ToyCars
//
//  Gameplay screen: RealityKit scene + HUD + controls, plus the three overlays
//  that frame a race - the loading card, the starting lights and the pause
//  panel.
//

import RealityKit
import SwiftUI
import simd

struct RaceView: View {
    @Bindable var app: AppModel
    let trackID: String

    @State private var ctrl = RaceController()
    @State private var showPause = false
    @State private var showTutorial = false
    @Environment(\.scenePhase) private var scenePhase
    private let showFPS = ProcessInfo.processInfo.arguments.contains("-fps")
    /// `-tut` used only to *decline* the debug skip, so on a profile that had
    /// already seen the tutorial it did nothing at all - which is not what
    /// "forces the intro tutorial" says. It now shows it whatever the profile
    /// remembers.
    private let forceTutorial = ProcessInfo.processInfo.arguments
        .contains("-tut")

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            if ctrl.ready, let engine = ctrl.engine {
                RealityView { content in
                    content.camera = .virtual
                    if let scene = ctrl.scene {
                        content.add(scene.root)
                        if let env = scene.environment {
                            content.environment = .skybox(env)
                        }
                    }
                    ctrl.startLoop()
                }
                .realityViewCameraControls(.none)
                .ignoresSafeArea()

                HUDView(engine: engine, ctrl: ctrl)
                let live = !ctrl.paused
                    && (engine.phase == .racing || engine.phase == .finished)
                ControlsOverlay(ctrl: ctrl, mode: app.progress.state.steerMode,
                                live: live)
                    .allowsHitTesting(live)

                if engine.phase == .countdown || engine.phase == .intro {
                    CountdownOverlay(engine: engine)
                }

                topBar(engine)

                if showTutorial {
                    TutorialOverlay(steerMode: app.progress.state.steerMode) {
                        showTutorial = false
                        ctrl.setPaused(false)
                        app.progress.markTutorialSeen()
                    }
                    .transition(.opacity)
                }
            } else if ctrl.loadFailed {
                LoadFailedView(trackID: trackID) { app.go(.tracks) }
            } else {
                LoadingView(progress: ctrl.loadProgress, trackID: trackID)
            }
        }
        .task {
            if forceTutorial || !app.progress.state.seenTutorial {
                showTutorial = true
                ctrl.setPaused(true)
            }
            await ctrl.setup(trackID: trackID,
                             carID: app.progress.state.selectedCar,
                             soundOn: app.progress.state.soundOn)
            // Captures the model and the track id and nothing else. Calling
            // an instance method here instead would capture the whole view,
            // and the view holds `ctrl` - so the controller would be holding
            // a closure that holds the controller, and no race would ever be
            // released. `RaceController` tears itself down before calling.
            ctrl.onRaceFinished = { [app, trackID] engine in
                RaceView.finish(engine, app: app, trackID: trackID)
            }
        }
        .onDisappear { ctrl.teardown() }
        .onChange(of: scenePhase) { _, phase in
            // leaving the app mid-race must not punish the player
            if phase != .active && ctrl.ready && !showPause {
                showPause = true
                ctrl.setPaused(true)
            }
        }
        .overlay {
            if showPause {
                PauseOverlay(
                    trackName: TrackCatalog.shared.track(trackID)?.name ?? "",
                    onResume: {
                        showPause = false
                        // The tutorial holds a pause of its own. Backgrounding
                        // the app while it is up raises this panel on top of
                        // it, and resuming from here would then start the race
                        // under a card the player has not read yet.
                        if !showTutorial { ctrl.setPaused(false) }
                    },
                    onRestart: {
                        showPause = false
                        Task {
                            await ctrl.restart(carID: app.progress.state.selectedCar)
                        }
                    },
                    onQuit: {
                        showPause = false
                        ctrl.teardown()
                        app.go(.tracks)
                    })
                .transition(.opacity)
            }
        }
    }

    private func topBar(_ engine: RaceEngine) -> some View {
        // On the race layer's scale, like the instruments it sits beside.
        GeometryReader { geo in
            let k = HUDView.scale(for: geo.size)
            VStack {
                HStack(alignment: .top, spacing: 8 * k) {
                    // Stacked, not in a row: in a row the badge runs into the
                    // position plate that starts just to the right of here.
                    VStack(alignment: .leading, spacing: 6 * k) {
                        Button {
                            showPause = true
                            ctrl.setPaused(true)
                            Haptics.tap()
                        } label: { Image(systemName: "pause.fill") }
                            .buttonStyle(GlassIconButtonStyle(size: 38 * k))
                            .accessibilityLabel("Pause")
                        if showFPS { FPSBadge(ctrl: ctrl) }
                    }
                    Spacer()
                }
                .padding(.horizontal, 14 * k)
                .padding(.top, 10 * k)
                Spacer()
            }
        }
    }

    /// Static, and given everything it needs, so that the closure the
    /// controller stores cannot reach back into the view - see where it is
    /// installed above. The race is already torn down by the time this runs,
    /// so nothing on screen moves while the catch-up works.
    private static func finish(_ engine: RaceEngine, app: AppModel,
                               trackID: String) {
        // The player's own result is read before the catch-up so the
        // simulation can no longer change it.
        let player = engine.playerCar
        let totalTime = player.finishTime
        let bestLap = player.bestLap
        let didFinish = player.finished
        Task { @MainActor in
            await engine.catchUpToFinish()
            // Read after the catch-up: a retirement drops down the order as
            // the rest of the field comes home, and the payout has to be
            // based on the placing shown in the table beside it.
            let position = max(1, engine.playerFinalPosition)
            app.lastOutcome = app.progress.apply(
                trackID: trackID,
                position: position,
                totalTime: totalTime,
                bestLap: bestLap,
                finished: didFinish,
                fieldSize: engine.fieldSize)
            app.lastFinalOrder = engine.finalStandings
            app.go(.results)
        }
    }
}

// ---------------------------------------------------------------- loading

/// The loading card. A progress bar on its own leaves the player looking at a
/// progress bar; this shows where they are going - the circuit's own colours,
/// its map, its name - and a rotating driving tip, so the wait teaches
/// something. The bar is a strip of road with a car running along it.
struct LoadingView: View {
    var progress: Double
    var trackID: String

    @State private var tip = Int.random(in: 0..<LoadingView.tips.count)

    static let tips: [LocalizedStringKey] = [
        "Lift off before the corner, not in it - the car keeps its grip.",
        "The drift button pays off in hairpins, not in fast bends.",
        "Collect every canister you pass. Running dry ends the race.",
        "Blue arrows are boost pads. Line them up on the straight.",
        "Cutting the corner over the grass is slower than it looks.",
        "A ramp taken flat out carries you over the next bend.",
    ]

    private var track: TrackDef? { TrackCatalog.shared.track(trackID) }

    var body: some View {
        ZStack {
            Backdrop(palette: ScenePalette.forTheme(track?.theme),
                     showRoad: false, vignette: 0.55)

            GeometryReader { geo in
                let m = Metrics(geo.size)
                ZStack {
                    VStack(spacing: m.snug) {
                        LogoMark(scale: m.pt(22))
                            .opacity(0.9)

                        if let t = track {
                            VStack(spacing: m.hair) {
                                StrokeText(
                                    text: Text(verbatim: t.name.uppercased())
                                        .font(TC.title(m.pt(30))),
                                    width: m.pt(2.4), color: TC.ink,
                                    dropShadow: m.pt(3))
                                    .foregroundStyle(TC.metalGradient(TC.silver))
                                Text(t.localizedSubtitle)
                                    .font(TC.body(m.pt(12), .bold))
                                    .foregroundStyle(.white.opacity(0.8))
                            }
                        }

                        RoadProgressBar(progress: progress)
                            .frame(width: m.column * 0.79, height: m.pt(26))

                        Text("Preparing the track…")
                            .font(TC.label(m.pt(11)))
                            .foregroundStyle(.white.opacity(0.75))
                    }
                    // The card behind the text: a sunset sky is a beautiful
                    // backdrop and a terrible background for pale type, so
                    // the whole block gets its own dark ground.
                    .padding(.horizontal, m.pad * 1.9)
                    .padding(.vertical, m.item * 1.5)
                    .background(GlassSurface(corner: m.rPanel, opacity: 0.62))
                    .frame(maxWidth: .infinity, maxHeight: .infinity)

                    VStack {
                        Spacer()
                        HStack(spacing: m.tight * 2) {
                            Image(systemName: "lightbulb.fill")
                                .font(.system(size: m.pt(12), weight: .black))
                                .foregroundStyle(TC.sun)
                            Text(LoadingView.tips[tip])
                                .font(TC.body(m.pt(12), .semibold))
                                .foregroundStyle(.white.opacity(0.9))
                                .multilineTextAlignment(.leading)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                        .padding(.horizontal, m.pad).padding(.vertical, m.snug)
                        .frame(maxWidth: m.column * 1.27)
                        .background(GlassSurface(corner: m.rChip,
                                                 opacity: 0.52))
                        .padding(.bottom, m.edgeV)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
    }
}

// ---------------------------------------------------------------- load failure

/// The bundle is missing the circuit or the car. It should never happen, but
/// the loading card has no way off it - the pause button lives behind
/// `ctrl.ready` - so without this screen a bad build is a locked door.
struct LoadFailedView: View {
    var trackID: String
    var onBack: () -> Void

    var body: some View {
        ZStack {
            Backdrop(palette: ScenePalette.podium(nil),
                     showRoad: false, vignette: 0.6)

            GeometryReader { geo in
                let m = Metrics(geo.size)
                VStack(spacing: m.item) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .font(.system(size: m.pt(38), weight: .black))
                        .foregroundStyle(TC.sun)
                    VStack(spacing: m.tight) {
                        StrokeText(text: Text("TRACK UNAVAILABLE")
                            .font(TC.title(m.pt(26))),
                                   width: m.pt(2.4), color: TC.ink,
                                   dropShadow: m.pt(3))
                            .foregroundStyle(TC.metalGradient(TC.silver))
                        Text("This circuit could not be loaded. Pick another one.")
                            .font(TC.body(m.pt(12), .semibold))
                            .foregroundStyle(.white.opacity(0.85))
                            .multilineTextAlignment(.center)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    Button {
                        Haptics.tap(); GameAudio.shared.uiTap(); onBack()
                    } label: {
                        ActionLabel(title: "BACK TO TRACKS", icon: "map.fill",
                                    showChevron: false, size: m.btnPrimary)
                    }
                    .buttonStyle(ChunkyButtonStyle(color: TC.sun,
                                                   height: m.btnPrimary))
                }
                .frame(maxWidth: m.column)
                .padding(.horizontal, m.pad * 1.6)
                .padding(.vertical, m.group)
                .background(GlassSurface(corner: m.rPanel, opacity: 0.62))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }
}

/// The progress bar as a piece of road: kerbs down the sides, a dashed centre
/// line, and a car that drives along it as the track loads.
struct RoadProgressBar: View {
    var progress: Double

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let p = max(0, min(1, progress))
            ZStack(alignment: .leading) {
                Capsule().fill(TC.ink.opacity(0.55))
                Capsule()
                    .fill(LinearGradient(colors: [Color(hex: "#4a5060"),
                                                  Color(hex: "#33383f")],
                                         startPoint: .top, endPoint: .bottom))
                    .padding(2)

                // Centre line.
                HStack(spacing: h * 0.24) {
                    ForEach(0..<9, id: \.self) { _ in
                        Capsule().fill(TC.sun.opacity(0.75))
                            .frame(width: h * 0.30, height: 2.5)
                    }
                }
                .frame(width: w, alignment: .center)
                .mask(Capsule().padding(2))

                // Covered ground.
                Capsule()
                    .fill(LinearGradient(colors: [TC.lime, TC.mint],
                                         startPoint: .leading,
                                         endPoint: .trailing))
                    .frame(width: max(6, (w - 4) * p))
                    .padding(2)
                    .opacity(0.30)

                Image(systemName: "car.side.fill")
                    .font(.system(size: h * 0.52, weight: .black))
                    .foregroundStyle(.white)
                    .shadow(color: .black.opacity(0.5), radius: 2, y: 1)
                    .offset(x: max(0, (w - h * 1.4) * p) + h * 0.1)
                    .animation(.easeOut(duration: 0.3), value: p)
            }
            .overlay(Capsule().strokeBorder(.white.opacity(0.35),
                                            lineWidth: 1.2))
        }
    }
}

// ---------------------------------------------------------------- pause

struct PauseOverlay: View {
    var trackName: String
    var onResume: () -> Void
    var onRestart: () -> Void
    var onQuit: () -> Void

    var body: some View {
        ZStack {
            // A dark, slightly blurred scrim: the race is still there behind
            // it, which is the point of a pause.
            Color.black.opacity(0.55).ignoresSafeArea()
            HazardStripes(a: TC.ink.opacity(0.0), b: TC.sun.opacity(0.05),
                          pitch: 40)
                .ignoresSafeArea()

            GeometryReader { geo in
                let m = Metrics(geo.size)
                VStack(spacing: m.snug) {
                    VStack(spacing: m.hair) {
                        StrokeText(text: Text("PAUSED")
                            .font(TC.title(m.pt(34))),
                                   width: m.pt(2.6), color: TC.ink,
                                   dropShadow: m.pt(3))
                            .foregroundStyle(TC.metalGradient(TC.silver))
                        Text(verbatim: trackName.uppercased())
                            .font(TC.label(m.pt(11)))
                            .foregroundStyle(.white.opacity(0.65))
                    }
                    .padding(.bottom, m.hair)

                    Button {
                        Haptics.tap(); GameAudio.shared.uiTap(); onResume()
                    } label: {
                        ActionLabel(title: "RESUME", icon: "play.fill",
                                    showChevron: false, size: m.btnPrimary)
                    }
                    .buttonStyle(ChunkyButtonStyle(color: TC.lime,
                                                   height: m.btnPrimary,
                                                   sheen: true))
                    Button {
                        Haptics.tap(); GameAudio.shared.uiTap(); onRestart()
                    } label: {
                        ActionLabel(title: "RESTART", icon: "arrow.clockwise",
                                    showChevron: false, size: m.btnSecondary)
                    }
                    .buttonStyle(ChunkyButtonStyle(color: TC.sun,
                                                   height: m.btnSecondary))
                    Button {
                        Haptics.tap(); GameAudio.shared.uiTap(); onQuit()
                    } label: {
                        ActionLabel(title: "BACK TO TRACKS", icon: "map.fill",
                                    showChevron: false, size: m.btnSecondary)
                    }
                    .buttonStyle(ChunkyButtonStyle(color: TC.cream,
                                                   height: m.btnSecondary))
                }
                .frame(maxWidth: m.column)
                .padding(m.pad * 1.35)
                .background(PanelSurface(fill: TC.slate, corner: m.rPanel,
                                         glossy: false))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }
}

// ---------------------------------------------------------------- countdown

/// The start. Instead of a bare number, the real thing: a gantry of lamps that
/// light up one per second, and the moment they go out the race is on. The
/// number stays as well, large and outlined, because it is the clearest signal
/// on a small screen - but the lights are what makes the grid feel like a grid.
struct CountdownOverlay: View {
    let engine: RaceEngine
    @State private var pop = false

    /// The engine starts its clock a little over three seconds out so the
    /// grid settles before the first beep, so the raw ceiling briefly reads
    /// four. Clamped: a countdown that starts at four and skips to three is
    /// worse than one that holds on three for an extra moment.
    private var n: Int { min(3, Int(ceil(engine.countdown))) }
    /// Lamps lit: one at "3", two at "2", three at "1", none on GO.
    private var lit: Int { n > 0 ? min(3, 4 - n) : 0 }

    var body: some View {
        GeometryReader { geo in
            // The race layer's own scale, not `Metrics`: this sits over the
            // road with the instruments and the pedals, and all three have to
            // grow together or the lights end up a different size from the
            // gauges they are counting down to.
            let k = HUDView.scale(for: geo.size)
            VStack(spacing: 0) {
                if engine.phase == .countdown {
                    StartLights(lit: lit, go: n <= 0, scale: k)
                        // Below the lap timer, which owns the top centre.
                        .padding(.top, 54 * k)
                }

                Spacer()

                if engine.phase == .countdown {
                    Group {
                        if n > 0 {
                            StrokeText(text: Text(verbatim: "\(n)")
                                .font(TC.title(112 * k)),
                                       width: 5 * k, color: TC.ink,
                                       outer: .white, outerWidth: 3 * k,
                                       dropShadow: 7 * k)
                                .foregroundStyle(TC.metalGradient(TC.gold))
                        } else {
                            StrokeText(text: Text("GO!")
                                .font(TC.title(88 * k)),
                                       width: 4.5 * k, color: TC.ink,
                                       outer: .white, outerWidth: 3 * k,
                                       dropShadow: 6 * k)
                                .foregroundStyle(LinearGradient(
                                    colors: [TC.lime.lighter(0.25), TC.lime],
                                    startPoint: .top, endPoint: .bottom))
                        }
                    }
                    .scaleEffect(pop ? 1 : 1.55)
                    .opacity(pop ? 1 : 0.2)
                    .id(n)
                    .onAppear {
                        pop = false
                        withAnimation(.spring(response: 0.30,
                                              dampingFraction: 0.5)) {
                            pop = true
                        }
                    }
                } else {
                    HStack(spacing: 8 * k) {
                        Image(systemName: "flag.checkered")
                        Text("Get ready…").font(TC.title(24 * k))
                    }
                    .foregroundStyle(.white)
                    .padding(.horizontal, 18 * k)
                    .padding(.vertical, 9 * k)
                    .background(Capsule().fill(TC.ink.opacity(0.45)))
                }

                Spacer()
            }
            .frame(maxWidth: .infinity)
        }
        .animation(.spring(response: 0.3), value: n)
        .allowsHitTesting(false)
    }
}

/// The lamp gantry. Housings are always dark; a lit lamp gets a hot core and a
/// glow, and on GO every lamp turns green at once.
struct StartLights: View {
    var lit: Int
    var go: Bool
    var scale: CGFloat = 1

    var body: some View {
        let lamp = 34 * scale
        HStack(spacing: 11 * scale) {
            ForEach(0..<3, id: \.self) { i in
                let on = go || i < lit
                let color = go ? TC.lime : TC.cherry
                ZStack {
                    Circle().fill(TC.ink.opacity(0.9))
                    Circle().strokeBorder(TC.chrome.dark.opacity(0.9),
                                          lineWidth: 2 * scale)
                    if on {
                        Circle()
                            .fill(RadialGradient(
                                colors: [color.lighter(0.35), color,
                                         color.darker(0.2)],
                                center: UnitPoint(x: 0.38, y: 0.32),
                                startRadius: 0, endRadius: 20 * scale))
                            .padding(4 * scale)
                        Circle()
                            .fill(.white.opacity(0.5))
                            .frame(width: 7 * scale, height: 7 * scale)
                            .offset(x: -4 * scale, y: -5 * scale)
                            .blur(radius: 1.5 * scale)
                    } else {
                        Circle()
                            .fill(TC.ink.opacity(0.6))
                            .padding(4 * scale)
                    }
                }
                .frame(width: lamp, height: lamp)
                .shadow(color: on ? color.opacity(0.9) : .clear,
                        radius: 11 * scale)
                .animation(.easeOut(duration: 0.12), value: on)
            }
        }
        .padding(.horizontal, 12 * scale).padding(.vertical, 8 * scale)
        .background {
            ZStack {
                RoundedRectangle(cornerRadius: 14 * scale, style: .continuous)
                    .fill(TC.metalGradient(TC.chrome))
                RoundedRectangle(cornerRadius: 14 * scale, style: .continuous)
                    .strokeBorder(TC.ink.opacity(0.6), lineWidth: 1.5 * scale)
            }
            .compositingGroup()
            .shadow(color: .black.opacity(0.5), radius: 8 * scale, y: 4 * scale)
        }
    }
}
