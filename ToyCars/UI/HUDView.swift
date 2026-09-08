//
//  HUDView.swift
//  ToyCars
//
//  The race overlay. Everything here is read at a glance while the player is
//  busy with a corner, so the layout follows what is asked most often and how
//  urgently:
//
//    top left      where am I, which lap        (checked constantly)
//    under it      how much fuel                (checked when it matters)
//    top centre    lap time and the delta       (checked on the straight)
//    top right     the map                      (glanced at before a corner)
//    bottom centre speed and turbo              (felt, not read)
//
//  The corners are kept clear of the driving line: the thumbs live in the
//  bottom left and bottom right, and the middle of the screen is where the car
//  is, so nothing opaque goes there.
//
//  On top of the instruments sits a layer of screen effects - vignette, speed
//  lines, a turbo wash, a dust tint off the road, a red pulse on the last of
//  the fuel, a flash across the line. They are what makes 60 km/h feel like
//  60 km/h; the number alone does not.
//

import SwiftUI
import simd

struct HUDView: View {
    let engine: RaceEngine
    let ctrl: RaceController
    /// The screen effects are the layer that moves for its own sake - the
    /// streaks, the turbo wash, the fuel pulse and the flash across the line.
    /// The instruments stay exactly as they are: those are information.
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// Full-scale deflection: the car's own top speed with the turbo
    /// multiplier, on the same exaggerated toy scale `CarSim.speedKph` reads
    /// out. Per car rather than per track, so the needle always means the
    /// same thing - and the needle reaches the stop only under boost.
    private var maxSpeed: Float {
        max(60, Float(engine.playerCar.def.displayTopSpeed) * 1.34)
    }

    /// 0…1 across the top half of the speed range - the point at which the
    /// screen effects start to build.
    private var rush: Double {
        let f = Double(engine.playerSpeed) / Double(maxSpeed)
        return max(0, min(1, (f - 0.45) / 0.55))
    }

    var body: some View {
        GeometryReader { geo in
            let k = HUDView.scale(for: geo.size)
            ZStack {
                effects
                instruments(k, geo.size)
                messageStack(k)
            }
        }
        .allowsHitTesting(false)
    }

    /// How much bigger everything should be than on a phone. The HUD is laid
    /// out in points that suit a phone in landscape (about 880 x 400); a
    /// tablet has twice the area, and instruments left at phone sizes shrink
    /// into its corners. Capped, because a dial does not need to keep growing
    /// once it is comfortably readable.
    static func scale(for size: CGSize) -> CGFloat {
        min(1.45, max(1, min(size.width / 880, size.height / 400)))
    }

    // ------------------------------------------------------------ effects

    private var effects: some View {
        ZStack {
            // Always on, very light: pulls the eye to the middle of the road.
            Vignette(strength: 0.30, power: 0.62)

            if rush > 0.02 && !reduceMotion {
                SpeedLines(intensity: rush * 0.9,
                           phase: engine.raceTime * 1.6)
                    .opacity(0.9)
            }

            // Turbo: a cyan wash from the edges plus its own speed lines, so
            // a boost pad is felt even when the speedometer is not being read.
            if engine.playerBoost > 0 {
                RadialGradient(
                    stops: [.init(color: .clear, location: 0.35),
                            .init(color: TC.turbo.opacity(0.30), location: 1)],
                    center: .center, startRadius: 0, endRadius: 620)
                    .blendMode(.plusLighter)
                if !reduceMotion {
                    SpeedLines(intensity: 1, phase: engine.raceTime * 2.6,
                               color: TC.turbo)
                        .opacity(0.8)
                }
            }

            // Off the tarmac: warm dust creeping in from the edges.
            if engine.playerOffTrack {
                RadialGradient(
                    stops: [.init(color: .clear, location: 0.42),
                            .init(color: TC.amber.opacity(0.34), location: 1)],
                    center: .center, startRadius: 0, endRadius: 640)
            }

            // The last of the fuel: a red pulse. A stroked rectangle reads as
            // a bug; a gradient reads as a warning light.
            if engine.playerFuel < RaceEngine.lowFuelFraction {
                LowFuelPulse(animated: !reduceMotion)
            }

            // Crossing the line. `lapFlash` is zero until the first lap is
            // complete, so it has to be excluded or the flash fires on the
            // start line as the race begins.
            if engine.lapFlash > 0, engine.raceTime - engine.lapFlash < 0.45,
               !reduceMotion {
                LapFlash(progress: (engine.raceTime - engine.lapFlash) / 0.45)
            }
        }
        .ignoresSafeArea()
        .allowsHitTesting(false)
        // Decoration, not instruments: nothing here is worth reading out.
        .accessibilityHidden(true)
    }

    // ------------------------------------------------------------ dials

    private func instruments(_ k: CGFloat, _ size: CGSize) -> some View {
        VStack(spacing: 0) {
            HStack(alignment: .top, spacing: 10 * k) {
                // The pause button lives in the top left corner, so the
                // position plate starts clear of it.
                Spacer().frame(width: 46 * k)

                VStack(alignment: .leading, spacing: 6 * k) {
                    PositionPlate(position: engine.playerPosition,
                                  fieldSize: engine.fieldSize,
                                  lap: engine.playerLap,
                                  totalLaps: engine.totalLaps,
                                  scale: k)
                    FuelGauge(fuel: engine.playerFuel, width: 104, scale: k)
                }

                Spacer()

                LapTimerPanel(raceTime: engine.raceTime,
                              lastLap: engine.lastLapTime,
                              bestLap: engine.playerBestLap,
                              scale: k)

                Spacer()

                miniMap(k)
            }
            .padding(.horizontal, 14 * k)
            .padding(.top, 8 * k)

            Spacer()
        }
        // The bottom of the screen is busy: the steering pads own the left
        // corner, the pedals the right, and the player's own car sits in the
        // middle. The gauge goes in the gap between the car and the pedals -
        // centred, it would cover the car it is reporting on.
        .overlay(alignment: .bottomLeading) {
            SpeedoGauge(speed: engine.playerSpeed,
                        maxSpeed: maxSpeed,
                        boost: engine.playerBoost / 1.7,
                        size: 106 * k)
                .overlay(alignment: .top) {
                    if engine.playerBoost > 0 { turboFlag(k) }
                }
                .position(x: size.width * 0.605,
                          y: size.height - 62 * k)
        }
    }

    private func turboFlag(_ k: CGFloat) -> some View {
        HStack(spacing: 3) {
            Image(systemName: "bolt.fill")
                .font(.system(size: 9 * k, weight: .black))
            Text("TURBO").font(TC.label(10 * k))
        }
        .foregroundStyle(TC.ink)
        .padding(.horizontal, 8 * k).padding(.vertical, 3 * k)
        .background(Capsule().fill(LinearGradient(
            colors: [TC.turbo, Color(hex: "#9ff9ff")],
            startPoint: .top, endPoint: .bottom)))
        .shadow(color: TC.turbo.opacity(0.8), radius: 6)
        .offset(y: -12 * k)
        .transition(.scale.combined(with: .opacity))
    }

    /// The live map, in a bezel so it does not float on the road. The player's
    /// own dot is haloed; canisters are red pips, because when the tank is low
    /// they are the only thing on this map that matters.
    private func miniMap(_ k: CGFloat) -> some View {
        TrackMiniMap(track: engine.track, lineWidth: 5 * k,
                     roadColor: .white.opacity(0.95),
                     edgeColor: TC.ink.opacity(0.55),
                     style: .hud,
                     cars: engine.cars.map {
                         ($0.pos, $0.isPlayer ? TC.sun : $0.def.color,
                          $0.isPlayer)
                     },
                     cans: engine.pickups.filter { $0.active }.map { $0.pos },
                     startS: engine.track.file.startS)
        .frame(width: 92 * k, height: 92 * k)
        .padding(6 * k)
        .background(GlassSurface(corner: 18 * k, opacity: 0.40))
    }

    // ------------------------------------------------------------ messages

    /// Event text. Sits below the instruments and above the car, in outlined
    /// display type on a plate - the only way saturated text stays readable
    /// over grass, tarmac and snow alike.
    private func messageStack(_ k: CGFloat) -> some View {
        VStack(spacing: 6 * k) {
            Spacer().frame(height: 92 * k)

            if engine.wrongWay {
                banner(Text("WRONG WAY!"), color: TC.cherry,
                       icon: "exclamationmark.triangle.fill", size: 22 * k)
            }
            if engine.playerOutOfFuel {
                banner(Text("OUT OF FUEL — find a canister!"),
                       color: TC.cherry, icon: "fuelpump.slash.fill",
                       size: 17 * k)
            }
            // The engine localises its messages when it pushes them, so they
            // are shown verbatim rather than looked up a second time.
            ForEach(engine.messages) { m in
                banner(Text(verbatim: m.text), color: color(for: m.kind),
                       icon: icon(for: m.kind),
                       size: (m.kind == .warn ? 20 : 17) * k)
                    .transition(.scale(scale: 0.7).combined(with: .opacity))
            }
            Spacer()
        }
        .animation(.spring(response: 0.35, dampingFraction: 0.7),
                   value: engine.messages.count)
    }

    private func banner(_ text: Text, color: Color,
                        icon: String, size: CGFloat) -> some View {
        HStack(spacing: 7) {
            Image(systemName: icon)
                .font(.system(size: size * 0.7, weight: .black))
            text.font(TC.title(size))
        }
        .foregroundStyle(.white)
        .shadow(color: TC.ink.opacity(0.7), radius: 0, y: 1.5)
        .padding(.horizontal, 14).padding(.vertical, 6)
        .background {
            ZStack {
                Capsule().fill(LinearGradient(
                    colors: [color.opacity(0.92), color.darker(0.16)
                        .opacity(0.92)],
                    startPoint: .top, endPoint: .bottom))
                Capsule().strokeBorder(.white.opacity(0.45), lineWidth: 1.2)
            }
            .shadow(color: .black.opacity(0.4), radius: 5, y: 3)
        }
    }

    private func color(for k: RaceEngine.RaceMessage.Kind) -> Color {
        switch k {
        case .good: return TC.teal
        case .warn: return TC.amber
        case .info: return TC.azure
        }
    }

    private func icon(for k: RaceEngine.RaceMessage.Kind) -> String {
        switch k {
        case .good: return "checkmark.seal.fill"
        case .warn: return "exclamationmark.triangle.fill"
        case .info: return "info.circle.fill"
        }
    }
}

// ---------------------------------------------------------------- effects

/// A red glow breathing in from the edges while the tank is nearly empty.
private struct LowFuelPulse: View {
    /// Still lit, just not breathing, when the system asks for less motion -
    /// the warning is the point, the pulse only sharpens it.
    var animated = true
    @State private var on = false

    var body: some View {
        RadialGradient(
            stops: [.init(color: .clear, location: 0.40),
                    .init(color: TC.cherry.opacity(on ? 0.40 : 0.16),
                          location: 1)],
            center: .center, startRadius: 0, endRadius: 660)
            .onAppear {
                guard animated else { return }
                withAnimation(.easeInOut(duration: 0.7)
                    .repeatForever(autoreverses: true)) {
                    on = true
                }
            }
    }
}

/// A band of light sweeping down the screen as the car crosses the line.
private struct LapFlash: View {
    var progress: Double

    var body: some View {
        GeometryReader { geo in
            let p = max(0, min(1, progress))
            LinearGradient(
                colors: [.clear, TC.sun.opacity(0.55), .white.opacity(0.35),
                         .clear],
                startPoint: .top, endPoint: .bottom)
                .frame(height: geo.size.height * 0.7)
                .offset(y: -geo.size.height * 0.7
                        + p * geo.size.height * 1.7)
                .blendMode(.plusLighter)
                .opacity(1 - p * 0.4)
        }
    }
}
