//
//  Buttons.swift
//  ToyCars
//
//  Buttons are the parts of the interface a player touches most, so they get
//  the most material detail: a moulded plastic cap sitting on a darker base,
//  a gloss crown, a bevelled rim and an inset icon medallion. Pressing does
//  not just dim the cap - it travels down into its own base and squashes a
//  little, so the button has a thickness the eye can measure.
//
//  The travel is the whole trick. `depth` is how tall the base is; a press
//  moves the cap down by exactly that, which is why the shadow under the cap
//  vanishes at the same moment. Anything less and it reads as a flicker.
//

import SwiftUI

// ---------------------------------------------------------------- chunky

/// The primary action button: full width, icon medallion on the left, label
/// centred, chevron on the right when it leads somewhere.
struct ChunkyButtonStyle: ButtonStyle {
    var color: Color = TC.sun
    var textColor: Color = TC.ink
    var height: CGFloat = 58
    var wide: Bool = true
    var depth: CGFloat = 6
    /// A slow light sweep. On for the one button the player should press next.
    var sheen: Bool = false

    func makeBody(configuration: Configuration) -> some View {
        let r = height * 0.32
        let shape = RoundedRectangle(cornerRadius: r, style: .continuous)
        let down = configuration.isPressed

        return configuration.label
            .font(TC.title(height * 0.33))
            .foregroundStyle(textColor)
            .shadow(color: textColor == .white
                    ? TC.ink.opacity(0.35) : .white.opacity(0.35),
                    radius: 0, y: textColor == .white ? 1 : 1)
            .frame(maxWidth: wide ? .infinity : nil)
            .frame(height: height)
            .padding(.horizontal, wide ? 14 : 26)
            .background {
                ZStack {
                    // Base - the side wall the cap sits on.
                    shape
                        .fill(LinearGradient(
                            colors: [color.darker(0.24), color.darker(0.34)],
                            startPoint: .top, endPoint: .bottom))
                        .offset(y: depth)

                    // Cap.
                    ZStack {
                        shape.fill(TC.plasticGradient(color))
                        Gloss(shape: shape, strength: 0.45, extent: 0.48)
                        if sheen { Sheen(shape: shape, period: 3.8,
                                         strength: 0.40) }
                        Bevel(shape: shape, strength: 1, width: 1.6)
                    }
                    .compositingGroup()
                    .shadow(color: TC.contact.opacity(down ? 0 : 0.5),
                            radius: 2, y: down ? 0 : 2)
                    .offset(y: down ? depth : 0)
                }
            }
            .contentShape(shape)
            .scaleEffect(x: down ? 1.02 : 1, y: down ? 0.97 : 1)
            .shadow(color: TC.ambient.opacity(down ? 0.2 : 0.45),
                    radius: down ? 5 : 12, y: down ? 3 : 8)
            .animation(.spring(response: 0.16, dampingFraction: 0.62),
                       value: down)
    }
}

/// The label layout the menu buttons use: a medallion with the icon, the
/// title, and a trailing chevron. Kept separate from the style so a button can
/// still carry a plain string when that is all it needs.
struct ActionLabel: View {
    var title: LocalizedStringKey
    var icon: String
    var tint: Color = TC.ink
    var showChevron: Bool = true
    var size: CGFloat = 58

    var body: some View {
        HStack(spacing: 12) {
            ZStack {
                Circle().fill(TC.ink.opacity(0.16))
                Circle().strokeBorder(.white.opacity(0.30), lineWidth: 1)
                Image(systemName: icon)
                    .font(.system(size: size * 0.30, weight: .black))
                    .foregroundStyle(tint)
            }
            .frame(width: size * 0.60, height: size * 0.60)

            Text(title)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
            Spacer(minLength: 0)
            if showChevron {
                Image(systemName: "chevron.right")
                    .font(.system(size: size * 0.24, weight: .black))
                    .foregroundStyle(tint.opacity(0.45))
            }
        }
    }
}

// ---------------------------------------------------------------- round

/// A round chrome-rimmed button - back arrows, pause, carousel arrows.
struct CircleIconButtonStyle: ButtonStyle {
    var color: Color = .white
    var iconColor: Color = TC.ink
    var size: CGFloat = 46
    var metal: TC.Metal = TC.chrome

    func makeBody(configuration: Configuration) -> some View {
        let down = configuration.isPressed
        return configuration.label
            .font(.system(size: size * 0.40, weight: .black))
            .foregroundStyle(iconColor)
            .frame(width: size, height: size)
            .background {
                ZStack {
                    Circle()
                        .fill(TC.metalGradient(metal))
                        .offset(y: 3)
                    Circle().fill(TC.plasticGradient(color))
                    Gloss(shape: Circle(), strength: 0.55, extent: 0.5)
                    Circle().strokeBorder(
                        LinearGradient(colors: [metal.light, metal.dark],
                                       startPoint: .top, endPoint: .bottom),
                        lineWidth: 1.6)
                    Bevel(shape: Circle(), strength: 0.7, width: 1.2)
                }
                .compositingGroup()
                .offset(y: down ? 3 : 0)
                .shadow(color: TC.contact, radius: down ? 2 : 5,
                        y: down ? 1 : 3)
            }
            .contentShape(Circle())
            .scaleEffect(down ? 0.94 : 1)
            .animation(.spring(response: 0.17, dampingFraction: 0.6),
                       value: down)
    }
}

// ---------------------------------------------------------------- HUD glass

/// A translucent round button for use over live gameplay, where an opaque cap
/// would block too much of the road.
struct GlassIconButtonStyle: ButtonStyle {
    var size: CGFloat = 44
    var tint: Color = .white

    func makeBody(configuration: Configuration) -> some View {
        let down = configuration.isPressed
        return configuration.label
            .font(.system(size: size * 0.38, weight: .black))
            .foregroundStyle(tint)
            .frame(width: size, height: size)
            .background {
                ZStack {
                    Circle().fill(TC.ink.opacity(down ? 0.62 : 0.44))
                    Circle().strokeBorder(
                        LinearGradient(colors: [.white.opacity(0.5),
                                                .white.opacity(0.08)],
                                       startPoint: .top, endPoint: .bottom),
                        lineWidth: 1.2)
                }
            }
            .contentShape(Circle())
            .scaleEffect(down ? 0.92 : 1)
            .animation(.spring(response: 0.15, dampingFraction: 0.6),
                       value: down)
    }
}

// ---------------------------------------------------------------- selectors

/// A small square chip - used for the car strip in the garage and the track
/// strip in track selection. Shows paint colour, lock state and selection.
struct ChipButton: View {
    var color: Color
    var label: String
    var selected: Bool
    var locked: Bool
    /// Point sizes are multiplied by this. See `Metrics`.
    var scale: CGFloat = 1
    var action: () -> Void

    var body: some View {
        let r = 11 * scale
        return Button(action: action) {
            VStack(spacing: 3 * scale) {
                ZStack {
                    RoundedRectangle(cornerRadius: r, style: .continuous)
                        .fill(TC.plasticGradient(locked
                                                 ? TC.steel.darker(0.18)
                                                 : color))
                    Gloss(shape: RoundedRectangle(cornerRadius: r,
                                                  style: .continuous),
                          strength: 0.4, extent: 0.5)
                    if locked {
                        Image(systemName: "lock.fill")
                            .font(.system(size: 12 * scale, weight: .black))
                            .foregroundStyle(.white.opacity(0.85))
                    }
                    RoundedRectangle(cornerRadius: r, style: .continuous)
                        .strokeBorder(selected ? Color.white
                                               : Color.white.opacity(0.25),
                                      lineWidth: selected ? 2.5 : 1)
                }
                .frame(width: 40 * scale, height: 32 * scale)
                .shadow(color: TC.contact, radius: selected ? 5 : 2,
                        y: selected ? 3 : 1)

                Text(verbatim: label)
                    .font(TC.label(8 * scale))
                    .foregroundStyle(.white.opacity(selected ? 1 : 0.6))
                    .lineLimit(1)
            }
            .scaleEffect(selected ? 1.10 : 1)
            .animation(.spring(response: 0.3, dampingFraction: 0.7),
                       value: selected)
        }
        .buttonStyle(.plain)
    }
}

/// A segmented control drawn in the game's own material language, since the
/// system one looks out of place on a painted panel.
struct ToySegmented: View {
    var options: [LocalizedStringKey]
    @Binding var selection: Int
    var tint: Color = TC.sky

    var body: some View {
        HStack(spacing: 4) {
            ForEach(Array(options.enumerated()), id: \.offset) { i, opt in
                let on = i == selection
                Button {
                    Haptics.tap(); GameAudio.shared.uiTap()
                    withAnimation(.spring(response: 0.28,
                                          dampingFraction: 0.78)) {
                        selection = i
                    }
                } label: {
                    Text(opt)
                        .font(TC.body(13, .black))
                        .foregroundStyle(on ? .white : TC.inkSoft)
                        .frame(maxWidth: .infinity)
                        .frame(height: 33)
                        .background {
                            if on {
                                ZStack {
                                    Capsule().fill(TC.plasticGradient(tint))
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
        .padding(4)
        .background(Capsule().fill(TC.ink.opacity(0.08)))
        .overlay(Capsule().strokeBorder(TC.ink.opacity(0.10), lineWidth: 1))
    }
}

/// A toggle in the same language: a moulded slot with a chrome knob.
struct ToyToggle: View {
    var isOn: Bool
    var tint: Color = TC.mint
    var action: (Bool) -> Void

    var body: some View {
        Button {
            Haptics.tap(); GameAudio.shared.uiTap()
            action(!isOn)
        } label: {
            ZStack(alignment: isOn ? .trailing : .leading) {
                Capsule()
                    .fill(isOn ? AnyShapeStyle(TC.plasticGradient(tint))
                               : AnyShapeStyle(TC.ink.opacity(0.16)))
                Capsule()
                    .strokeBorder(TC.ink.opacity(0.18), lineWidth: 1)
                ZStack {
                    Circle().fill(TC.metalGradient(TC.silver))
                    Circle().strokeBorder(TC.ink.opacity(0.2), lineWidth: 1)
                }
                .frame(width: 26, height: 26)
                .padding(3)
                .shadow(color: TC.contact, radius: 3, y: 2)
            }
            .frame(width: 60, height: 32)
            .animation(.spring(response: 0.3, dampingFraction: 0.7),
                       value: isOn)
        }
        .buttonStyle(.plain)
    }
}
