//
//  TutorialOverlay.swift
//  ToyCars
//
//  A short primer before the first race. Shown only once, so it has to land
//  first time: four cards, each with the actual control it describes drawn on
//  it rather than a generic icon, so the player recognises the button when
//  they see it on the road half a minute later.
//

import SwiftUI

struct TutorialOverlay: View {
    var steerMode: Int
    var onDismiss: () -> Void
    @State private var appear = false

    var body: some View {
        ZStack {
            Color.black.opacity(0.6).ignoresSafeArea()

            // On `Metrics`, like every other screen. This was the one panel
            // still laid out in fixed points, so it was also the one panel
            // that shrank into the corner of a tablet.
            GeometryReader { geo in
                let m = Metrics(geo.size)
                VStack(spacing: m.item) {
                    HStack(spacing: m.snug) {
                        Image(systemName: "flag.checkered")
                            .font(.system(size: m.pt(15), weight: .black))
                            .foregroundStyle(TC.sun)
                        StrokeText(text: Text("HOW TO PLAY")
                            .font(TC.title(m.pt(22))),
                                   width: m.pt(2), color: TC.ink,
                                   dropShadow: m.pt(2))
                            .foregroundStyle(TC.metalGradient(TC.gold))
                    }

                    HStack(spacing: m.snug) {
                        tip(icon: steerMode == 0 ? "arrow.left.and.right"
                                                 : "hand.draw.fill",
                            title: "Steering",
                            text: steerMode == 0
                                ? "Hold your finger in the lower-left part of the screen. Left of centre steers left, right of centre steers right."
                                : "Drag your finger across the left half of the screen.",
                            color: TC.sky, m: m)
                        tip(icon: "flame.fill", title: "Drift",
                            text: "The purple button lets the rear end slide. In tight corners that is faster.",
                            color: TC.grape, m: m)
                    }
                    HStack(spacing: m.snug) {
                        tip(icon: "fuelpump.fill", title: "Fuel",
                            text: "The throttle is automatic and fuel drains away. Collect the red canisters on the track or you will not make it.",
                            color: TC.cherry, m: m)
                        tip(icon: "bolt.fill", title: "Turbo",
                            text: "The blue arrows on the track give a short boost. Ramps cut the route short.",
                            color: TC.mint, m: m)
                    }

                    Button {
                        Haptics.tap(); GameAudio.shared.uiTap()
                        onDismiss()
                    } label: {
                        ActionLabel(title: "LET'S GO!", icon: "flag.checkered",
                                    showChevron: false, size: m.btnPrimary)
                    }
                    .buttonStyle(ChunkyButtonStyle(color: TC.lime,
                                                   height: m.btnPrimary,
                                                   sheen: true))
                    .frame(maxWidth: m.column)
                }
                .padding(m.pad * 1.25)
                .background(PanelSurface(fill: TC.slate, corner: m.rPanel,
                                         glossy: false))
                // Capped: two columns of tips stretched across a tablet put
                // four words on each line and the rest in white space.
                .frame(maxWidth: m.column * 1.9)
                .padding(.horizontal, m.edge)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .scaleEffect(appear ? 1 : 0.88)
            .opacity(appear ? 1 : 0)
        }
        .onAppear {
            withAnimation(.spring(response: 0.42, dampingFraction: 0.75)) {
                appear = true
            }
        }
    }

    private func tip(icon: String, title: LocalizedStringKey,
                     text: LocalizedStringKey, color: Color,
                     m: Metrics) -> some View {
        HStack(alignment: .top, spacing: m.snug) {
            ZStack {
                Circle().fill(TC.plasticGradient(color))
                Gloss(shape: Circle(), strength: 0.45, extent: 0.5)
                Circle().strokeBorder(.white.opacity(0.35),
                                      lineWidth: m.pt(1.2))
                Image(systemName: icon)
                    .font(.system(size: m.pt(16), weight: .black))
                    .foregroundStyle(.white)
            }
            .frame(width: m.pt(36), height: m.pt(36))
            .shadow(color: .black.opacity(0.35), radius: m.pt(3), y: 2)

            VStack(alignment: .leading, spacing: m.hair * 0.5) {
                Text(title)
                    .font(TC.body(m.pt(14), .black))
                    .foregroundStyle(.white)
                Text(text)
                    .font(TC.body(m.pt(11), .medium))
                    .foregroundStyle(.white.opacity(0.78))
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(m.snug)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background {
            RoundedRectangle(cornerRadius: m.rChip, style: .continuous)
                .fill(.white.opacity(0.06))
                .overlay(RoundedRectangle(cornerRadius: m.rChip,
                                          style: .continuous)
                    .strokeBorder(.white.opacity(0.10), lineWidth: 1))
        }
    }
}

/// Small badge showing the current FPS - enabled with the -fps argument.
struct FPSBadge: View {
    let ctrl: RaceController

    var body: some View {
        Text(verbatim: String(format: "%.0f fps", ctrl.fps))
            .font(TC.readout(11))
            .foregroundStyle(ctrl.fps < 45 ? TC.cherry : TC.mint)
            .padding(.horizontal, 8).padding(.vertical, 4)
            .background(Capsule().fill(TC.ink.opacity(0.55)))
    }
}
