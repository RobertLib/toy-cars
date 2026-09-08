//
//  ControlsOverlay.swift
//  ToyCars
//
//  The controls are deliberately forgiving: the whole lower-left part of the
//  screen is one big steering pad. The split point sits exactly between the two
//  arrows, so touching an arrow gives full lock and touching between them a
//  gentle correction. The player never has to hit a small target and can slide
//  a finger from one side to the other. The car handles the throttle itself.
//
//  Visually the controls are moulded rubber pads rather than flat glyphs: a
//  dark cup, a coloured cap, a gloss crown and a rim that lights up under the
//  finger. Two details do the work of making them feel physical - the cap
//  travels down into its cup when pressed, and the steering pads brighten in
//  proportion to how much lock is being asked for, so the player can see the
//  input they are giving rather than only its result on the car.
//

import SwiftUI

struct ControlsOverlay: View {
    let ctrl: RaceController
    var mode: Int
    /// Whether the controls are actually driving the car. A pedal is released
    /// by the end of its drag gesture, and a gesture that is *cancelled* -
    /// the pause panel coming up under the thumb, a system edge swipe - never
    /// ends. The pad would stay lit and the brake stay on. This is the signal
    /// to let go of everything by hand.
    var live: Bool = true

    // Steering pad dimensions, in phone-landscape points and then scaled with
    // the screen exactly as the HUD is (`HUDView.scale`), so the controls stay
    // the same size relative to the road on a tablet. Both the graphics and
    // the gesture are derived from these, so the two can never drift apart.
    private static let baseEdge: CGFloat = 26   // inset from the screen edge
    private static let baseArrow: CGFloat = 82  // arrow diameter
    private static let baseGap: CGFloat = 16    // gap between the arrows

    @State private var k: CGFloat = 1

    private var edge: CGFloat { Self.baseEdge * k }
    private var arrow: CGFloat { Self.baseArrow * k }
    private var gap: CGFloat { Self.baseGap * k }

    private var leftCenterX: CGFloat { edge + arrow / 2 }
    private var rightCenterX: CGFloat { edge + arrow * 1.5 + gap }
    private var splitX: CGFloat { (leftCenterX + rightCenterX) / 2 }
    /// Distance from the split point that already means full lock.
    private var fullLockDistance: CGFloat { (rightCenterX - leftCenterX) / 2 }

    private static let space = "steerPad"

    @State private var steerVisual: Float = 0
    @State private var dragOrigin: CGFloat? = nil

    var body: some View {
        GeometryReader { geo in
            let padW = mode == 0 ? min(geo.size.width * 0.50, 430 * k)
                                 : geo.size.width * 0.58
            let padH = min(geo.size.height * 0.62, 360 * k)
            let bottomY = geo.size.height - edge - arrow / 2

            ZStack(alignment: .topLeading) {
                // ------------------------------------------- touch zone
                Color.clear
                    .contentShape(Rectangle())
                    .frame(width: padW, height: padH)
                    .offset(x: 0, y: geo.size.height - padH)
                    .gesture(steerGesture())
                    .accessibilityLabel("Steering")

                // ------------------------------------------- steering
                if mode == 0 {
                    SteerPad(icon: "arrowtriangle.left.fill",
                             amount: max(0, -Double(steerVisual)),
                             size: arrow)
                        .position(x: leftCenterX, y: bottomY)
                    SteerPad(icon: "arrowtriangle.right.fill",
                             amount: max(0, Double(steerVisual)),
                             size: arrow)
                        .position(x: rightCenterX, y: bottomY)
                } else {
                    DragSteerIndicator(amount: Double(steerVisual), scale: k)
                        .position(x: leftCenterX + arrow * 0.7, y: bottomY)
                }

                // ------------------------------------------- pedals
                HStack(alignment: .bottom, spacing: 12 * k) {
                    PedalButton(icon: "flame.fill", color: TC.grape,
                                size: 64 * k, label: "DRIFT",
                                scale: k, live: live) { p in
                        ctrl.drifting = p
                    }
                    PedalButton(icon: "hand.raised.fill",
                                color: TC.cherry, size: 84 * k,
                                label: "BRAKE", scale: k, live: live) { p in
                        ctrl.braking = p
                    }
                }
                // Raised enough that the caption under each pedal is not
                // clipped by the bottom of the screen.
                .position(x: geo.size.width - 128 * k,
                          y: geo.size.height - edge - 62 * k)
            }
            .frame(width: geo.size.width, height: geo.size.height)
            .onAppear { k = HUDView.scale(for: geo.size) }
            .onChange(of: geo.size) { _, s in k = HUDView.scale(for: s) }
        }
        .coordinateSpace(name: ControlsOverlay.space)
        .onChange(of: live) { _, on in
            guard !on else { return }
            dragOrigin = nil
            steerVisual = 0
            ctrl.steerInput = 0
        }
    }

    // ------------------------------------------------------------ gesture

    private func steerGesture() -> some Gesture {
        DragGesture(minimumDistance: 0,
                    coordinateSpace: .named(ControlsOverlay.space))
            .onChanged { v in
                let s: Float
                if mode == 0 {
                    let dx = v.location.x - splitX
                    if abs(dx) < 8 {
                        s = 0
                    } else {
                        s = Float(max(-1, min(1, dx / fullLockDistance)))
                    }
                } else {
                    if dragOrigin == nil {
                        dragOrigin = v.startLocation.x
                        Haptics.tap()
                    }
                    let dx = v.location.x - (dragOrigin ?? 0)
                    s = Float(max(-1, min(1, dx / 74)))
                }
                if mode == 0, (s < 0) != (steerVisual < 0), s != 0 {
                    Haptics.tap()
                }
                ctrl.steerInput = s
                steerVisual = s
            }
            .onEnded { _ in
                dragOrigin = nil
                ctrl.steerInput = 0
                steerVisual = 0
            }
    }
}

// ---------------------------------------------------------------- steering

/// One steering pad. `amount` is how much lock is being asked for on this
/// side, 0…1: the cap brightens, the rim lights and an arc fills around the
/// edge, so partial lock looks like partial lock.
private struct SteerPad: View {
    var icon: String
    var amount: Double
    var size: CGFloat

    private var active: Bool { amount > 0.08 }

    var body: some View {
        ZStack {
            // Cup.
            Circle()
                .fill(TC.ink.opacity(0.42))
                .frame(width: size, height: size)

            // Lock arc. Struck off `size` like the speedometer's sweep, so
            // the pad reads the same on a phone and on a tablet - a 4-point
            // arc around an 82-point pad is not a 4-point arc around a
            // 119-point one.
            ArcShape(start: 130, sweep: 280 * amount, inset: size * 0.037)
                .stroke(LinearGradient(colors: [TC.sun, TC.amber],
                                       startPoint: .top, endPoint: .bottom),
                        style: StrokeStyle(lineWidth: size * 0.049,
                                           lineCap: .round))
                .frame(width: size, height: size)
                .opacity(active ? 1 : 0)

            // Cap.
            Circle()
                .fill(LinearGradient(
                    colors: active
                        ? [.white, Color(hex: "#e8eef7")]
                        : [.white.opacity(0.60), .white.opacity(0.40)],
                    startPoint: .top, endPoint: .bottom))
                .frame(width: size * 0.82, height: size * 0.82)
                .overlay(
                    Circle().strokeBorder(.white.opacity(active ? 0.95 : 0.45),
                                          lineWidth: size * 0.017)
                        .frame(width: size * 0.82, height: size * 0.82))
                .overlay(
                    Gloss(shape: Circle(), strength: 0.5, extent: 0.5)
                        .frame(width: size * 0.82, height: size * 0.82))
                .offset(y: active ? size * 0.024 : 0)

            Image(systemName: icon)
                .font(.system(size: size * 0.34, weight: .black))
                .foregroundStyle(active ? TC.ink : TC.ink.opacity(0.55))
                .offset(y: active ? size * 0.024 : 0)
        }
        .shadow(color: .black.opacity(0.32), radius: size * 0.061,
                y: size * 0.037)
        .scaleEffect(active ? 0.95 : 1)
        .animation(.spring(response: 0.15, dampingFraction: 0.6),
                   value: active)
        .allowsHitTesting(false)
    }
}

/// In drag mode there are no pads, so the current input is shown as a slider
/// with a lit bar running out from the centre.
private struct DragSteerIndicator: View {
    var amount: Double
    var scale: CGFloat = 1

    var body: some View {
        VStack(spacing: 6 * scale) {
            ZStack(alignment: .center) {
                Capsule().fill(TC.ink.opacity(0.42))
                    .frame(width: 150 * scale, height: 12 * scale)
                Capsule()
                    .fill(LinearGradient(colors: [TC.sun, TC.amber],
                                         startPoint: .leading,
                                         endPoint: .trailing))
                    .frame(width: max(2, 75 * scale * abs(amount)),
                           height: 8 * scale)
                    .offset(x: (amount > 0 ? 1 : -1)
                            * 75 * scale * abs(amount) / 2)
                Capsule().fill(.white.opacity(0.7))
                    .frame(width: 2, height: 16 * scale)
            }
            HStack(spacing: 6 * scale) {
                Image(systemName: "hand.draw.fill")
                    .font(.system(size: 11 * scale, weight: .black))
                Text("drag your finger")
                    .font(TC.label(11 * scale))
            }
            .foregroundStyle(.white.opacity(0.75))
            .padding(.horizontal, 10).padding(.vertical, 5)
            .background(Capsule().fill(TC.ink.opacity(0.35)))
            .opacity(abs(amount) > 0.05 ? 0.25 : 1)
        }
        .allowsHitTesting(false)
    }
}

// ---------------------------------------------------------------- pedals

/// A pedal: a coloured cap in a dark cup that sinks under the thumb. The touch
/// target is a third larger than the cap, so a thumb that lands slightly off
/// still counts.
private struct PedalButton: View {
    let icon: String
    let color: Color
    let size: CGFloat
    let label: LocalizedStringKey
    /// Screen scale, the HUD's own (`HUDView.scale`) - not derived from
    /// `size`, because the two pedals are deliberately different sizes and
    /// their captions have to match each other. The caption used to be a flat
    /// nine points while the cap grew with the screen, so on a tablet the one
    /// word telling the player what the pedal does was the only thing in the
    /// controls that had not grown with the road.
    var scale: CGFloat = 1
    var live: Bool = true
    let action: (Bool) -> Void
    @State private var down = false

    var body: some View {
        VStack(spacing: 5 * scale) {
            ZStack {
                // Cup and its rim.
                Circle()
                    .fill(TC.ink.opacity(0.45))
                    .frame(width: size * 1.10, height: size * 1.10)
                Circle()
                    .strokeBorder(.white.opacity(down ? 0.6 : 0.22),
                                  lineWidth: 1.5 * scale)
                    .frame(width: size * 1.10, height: size * 1.10)

                // Cap.
                ZStack {
                    Circle().fill(TC.plasticGradient(color))
                    Gloss(shape: Circle(), strength: 0.5, extent: 0.5)
                    Circle().strokeBorder(.white.opacity(0.45),
                                          lineWidth: 1.4 * scale)
                    Image(systemName: icon)
                        .font(.system(size: size * 0.36, weight: .black))
                        .foregroundStyle(.white)
                        .shadow(color: color.darker(0.3).opacity(0.7),
                                radius: 0, y: 1)
                }
                .frame(width: size, height: size)
                .compositingGroup()
                .shadow(color: .black.opacity(down ? 0.1 : 0.35),
                        radius: down ? 2 : 5, y: down ? 1 : 3)
                .offset(y: down ? 3 : 0)
                .scaleEffect(down ? 0.94 : 1)

                if down {
                    Circle()
                        .strokeBorder(color.lighter(0.25),
                                      lineWidth: 2.5 * scale)
                        .frame(width: size * 1.10, height: size * 1.10)
                        .shadow(color: color.opacity(0.8), radius: 6 * scale)
                }
            }
            Text(label)
                .font(TC.label(9 * scale))
                .foregroundStyle(.white.opacity(down ? 1 : 0.75))
        }
        .animation(.spring(response: 0.15, dampingFraction: 0.6), value: down)
        .contentShape(Circle().scale(1.35))
        // One element with the pedal's own name on it, rather than a pile of
        // unlabelled circles and a caption.
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label)
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { _ in
                    if !down { down = true; Haptics.tap(); action(true) }
                }
                .onEnded { _ in down = false; action(false) }
        )
        // A cancelled gesture never ends, so the release has to come from
        // somewhere: the controls being taken away, or the pad going off
        // screen with a thumb still on it.
        .onChange(of: live) { _, on in if !on { release() } }
        .onDisappear { release() }
    }

    private func release() {
        guard down else { return }
        down = false
        action(false)
    }
}
