//
//  Theme.swift
//  ToyCars
//
//  The shared visual language.
//
//  The art direction is "die-cast toybox": the game is about little toy cars,
//  so the interface is built out of the materials a toy is made of - glossy
//  injection-moulded plastic, chrome trim, enamel paint, printed cardboard and
//  metal medals. Everything on screen is therefore a *surface* with a light
//  side and a shadow side rather than a flat rectangle, which is what gives
//  the arcade racers this UI is measured against (Beach Buggy Racing, Mario
//  Kart Tour, Asphalt) their tactile look.
//
//  Three rules keep it consistent:
//
//  1. One light direction - from above. Highlights sit on top edges, contact
//     shadows underneath. `Bevel` and `Gloss` in Materials.swift do this.
//  2. Display type is always outlined. Saturated colours on saturated
//     backdrops need a dark keyline to stay readable; `StrokeText` provides it.
//  3. Nothing is pure grey. Shadows are tinted with `TC.ink` (a navy), lights
//     with `TC.cream` (a warm white), so the whole screen reads as one scene.
//

import SwiftUI

enum TC {
    // ------------------------------------------------------------ neutrals
    /// Darkest ink. Outlines, body copy on light panels, shadow tint.
    static let ink = Color(hex: "#131a2c")
    static let inkSoft = Color(hex: "#5a6478")
    /// Panel colour for dark surfaces (pause, tutorial, HUD glass).
    static let slate = Color(hex: "#212b44")
    static let steel = Color(hex: "#8e9bb3")
    static let cream = Color(hex: "#fff6e8")
    static let paper = Color(hex: "#ffffff")

    // ------------------------------------------------------------ toy paint
    static let sun = Color(hex: "#ffc21c")
    static let amber = Color(hex: "#ff9f1c")
    static let tangerine = Color(hex: "#ff7a2f")
    static let cherry = Color(hex: "#ee2f45")
    static let rose = Color(hex: "#ff5d7a")
    static let mint = Color(hex: "#22d3a5")
    static let teal = Color(hex: "#0fb5a0")
    static let sky = Color(hex: "#35a7f2")
    static let azure = Color(hex: "#1d6fd0")
    static let grape = Color(hex: "#8b5cf6")
    static let lime = Color(hex: "#7cd94f")
    static let turbo = Color(hex: "#39e6ff")

    // ------------------------------------------------------------ metals
    /// A medal or trim gradient: light band, body, dark band. Used by
    /// `metalGradient` so every metal surface catches the light the same way.
    struct Metal: Sendable {
        let light: Color, body: Color, dark: Color
    }
    static let gold = Metal(light: Color(hex: "#fff0b4"),
                            body: Color(hex: "#ffc21c"),
                            dark: Color(hex: "#a86a12"))
    static let silver = Metal(light: Color(hex: "#ffffff"),
                              body: Color(hex: "#c8d3e4"),
                              dark: Color(hex: "#6c7789"))
    static let bronze = Metal(light: Color(hex: "#f7cda3"),
                              body: Color(hex: "#cd7f43"),
                              dark: Color(hex: "#7d4218"))
    static let chrome = Metal(light: Color(hex: "#ffffff"),
                              body: Color(hex: "#aebbcf"),
                              dark: Color(hex: "#4c5a72"))

    /// The medal for a finishing position (nil past third place).
    static func medal(for position: Int) -> Metal? {
        switch position {
        case 1: return gold
        case 2: return silver
        case 3: return bronze
        default: return nil
        }
    }

    /// A metal surface lit from the top: bright rim, hard light band a third
    /// of the way down, then a fall-off into the dark tone.
    static func metalGradient(_ m: Metal,
                              from: UnitPoint = .top,
                              to: UnitPoint = .bottom) -> LinearGradient {
        LinearGradient(stops: [
            .init(color: m.light, location: 0.00),
            .init(color: m.body.lighter(0.10), location: 0.28),
            .init(color: m.body, location: 0.40),
            .init(color: m.dark, location: 0.86),
            .init(color: m.dark.lighter(0.16), location: 1.00),
        ], startPoint: from, endPoint: to)
    }

    /// Moulded plastic: the paint colour with a lighter crown and a darker
    /// base, so a filled shape reads as domed rather than flat.
    static func plasticGradient(_ c: Color) -> LinearGradient {
        LinearGradient(stops: [
            .init(color: c.lighter(0.26), location: 0.00),
            .init(color: c.lighter(0.06), location: 0.42),
            .init(color: c, location: 0.62),
            .init(color: c.darker(0.12), location: 1.00),
        ], startPoint: .top, endPoint: .bottom)
    }

    // ------------------------------------------------------------ type
    /// Display type: heavy, rounded, tight. Always pair with `StrokeText`.
    static func title(_ size: CGFloat) -> Font {
        .system(size: size, weight: .black, design: .rounded)
    }
    static func body(_ size: CGFloat, _ w: Font.Weight = .semibold) -> Font {
        .system(size: size, weight: w, design: .rounded)
    }
    /// Readouts - times, speeds, coin counts. Monospaced so digits do not
    /// jitter as they change.
    static func readout(_ size: CGFloat, _ w: Font.Weight = .black) -> Font {
        .system(size: size, weight: w, design: .rounded).monospacedDigit()
    }
    /// Small all-caps labels above a value.
    static func label(_ size: CGFloat = 10) -> Font {
        .system(size: size, weight: .black, design: .rounded)
    }

    // ------------------------------------------------------------ depth
    /// Contact shadow - tight and dark, sits directly under the object.
    static let contact = Color.black.opacity(0.32)
    /// Ambient shadow - wide and soft, grounds the object in the scene.
    static let ambient = Color(hex: "#0b1020").opacity(0.28)

    /// Corner radius scale. Toy parts are moulded, so nothing is sharp.
    static let rCard: CGFloat = 22
    static let rPanel: CGFloat = 28
}

// ---------------------------------------------------------------- layout

/// The spacing, sizing and type scale a screen lays itself out on.
///
/// The game is landscape-only, but that still spans a 375-point-tall phone to
/// a 1024-point-tall tablet - the short edge, which is what a landscape layout
/// actually has to fit into, nearly triples. No fixed set of point sizes can
/// serve both ends: padding that breathes on the tablet eats the phone, and a
/// badge sized for the phone is lost on the tablet. So every screen builds one
/// `Metrics` from its `GeometryReader` and takes each gap, margin, control
/// height and type size from it.
///
/// Three rules keep the result from looking like a zoomed phone:
///
/// 1. **Space grows faster than type.** Going from phone to tablet roughly
///    doubles the gaps but moves a title by only a third. Reading distance
///    barely changes between the two - a tablet is held a little further away,
///    not twice as far - so what a big screen buys is room around things, not
///    bigger things.
/// 2. **The steps are ratios, not increments.** Each gap in the scale is about
///    1.5x the one below it, so any two steps read as unmistakably different.
///    This is what makes grouping work at all: `group` has to be clearly wider
///    than `item`, or the eye cannot tell where one group ends and the next
///    begins, and the panel reads as one undifferentiated mass.
/// 3. **Nothing spans the full width just because it can.** Rows of controls
///    and columns of text are capped at `column`; a button stretched across a
///    tablet reads as a banner rather than as something to press.
struct Metrics {
    let size: CGSize
    /// 0 on the shortest phone in landscape, 1 on a tablet. Everything below
    /// interpolates along it.
    let t: CGFloat

    init(_ size: CGSize) {
        self.size = size
        // The short edge is the one that decides a landscape layout: it is
        // what the header, the content band and the footer have to share.
        //
        // The floor is the smallest screen the game actually runs on, and
        // that is a 12/13 mini at **360** points across in landscape - not
        // the 375/380 of the phone most likely to be sitting on the desk.
        // Bottoming out at 380 made the scale flat across that whole band,
        // so the two smallest phones were handed a layout tuned for the
        // larger of them and a panel that only just fitted one ran off the
        // bottom of the other. Settings did exactly that.
        let short = min(size.width, size.height)
        t = max(0, min(1, (short - 360) / (820 - 360)))
    }

    private func lerp(_ a: CGFloat, _ b: CGFloat) -> CGFloat { a + (b - a) * t }

    // ------------------------------------------------------------ type

    /// Multiplier for every point size on the screen. Shallower than the
    /// space scale on purpose - see rule 1.
    var type: CGFloat { lerp(1, 1.34) }

    /// A point size on the type scale, scaled for this screen.
    func pt(_ size: CGFloat) -> CGFloat { size * type }

    // ------------------------------------------------------------ spacing

    /// Between a label and the value it belongs to.
    var hair: CGFloat { lerp(2, 3) }
    /// Inside one small component - an icon and its text.
    var tight: CGFloat { lerp(4, 6) }
    /// Between neighbours that belong together - two buttons in a row.
    var snug: CGFloat { lerp(8, 12) }
    /// Between rows of one group - the lines of a stat block.
    var item: CGFloat { lerp(12, 18) }
    /// Between groups inside a panel. Must read as clearly wider than `item`.
    var group: CGFloat { lerp(20, 30) }
    /// Between the major blocks of a screen.
    var section: CGFloat { lerp(30, 46) }

    // ------------------------------------------------------------ margins

    /// Screen side margin.
    var edge: CGFloat { lerp(20, 40) }
    /// Screen top and bottom margin. Smaller than `edge`: landscape is short
    /// of height and long on width, so the budget goes where there is room.
    var edgeV: CGFloat { lerp(14, 28) }
    /// Between the two columns of a landscape screen.
    var gutter: CGFloat { lerp(16, 30) }
    /// Inner padding of a panel.
    var pad: CGFloat { lerp(16, 26) }

    // ------------------------------------------------------------ radii

    var rChip: CGFloat { lerp(14, 20) }
    var rCard: CGFloat { lerp(20, 30) }
    var rPanel: CGFloat { lerp(26, 38) }

    // ------------------------------------------------------------ controls

    /// The one button the player should press next.
    var btnPrimary: CGFloat { lerp(54, 74) }
    /// Everything else with a label on it.
    var btnSecondary: CGFloat { lerp(44, 60) }
    /// A round icon button - back, close, pause.
    var btnIcon: CGFloat { lerp(40, 54) }

    /// The widest a column of controls or text is allowed to get - see rule 3.
    var column: CGFloat { lerp(330, 470) }
    /// The side panel of a two-column screen, as a share of the width but
    /// never so narrow that a name and a time collide.
    var sidePanel: CGFloat {
        max(lerp(292, 340), min(lerp(390, 470), size.width * 0.35))
    }
}

// ---------------------------------------------------------------- colour maths

extension Color {
    func lighter(_ amount: Double) -> Color {
        let ui = UIColor(self)
        var h: CGFloat = 0, s: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        guard ui.getHue(&h, saturation: &s, brightness: &b, alpha: &a) else {
            return self
        }
        return Color(UIColor(hue: h, saturation: max(0, s - amount * 0.35),
                             brightness: min(1, b + amount), alpha: a))
    }

    func darker(_ amount: Double) -> Color {
        let ui = UIColor(self)
        var h: CGFloat = 0, s: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        guard ui.getHue(&h, saturation: &s, brightness: &b, alpha: &a) else {
            return self
        }
        return Color(UIColor(hue: h, saturation: min(1, s + amount * 0.12),
                             brightness: max(0, b - amount), alpha: a))
    }

    /// Linear mix towards another colour. `amount` 0 keeps self, 1 returns the
    /// other colour. Used for aerial haze and for tinting panels by car paint.
    func blended(with other: Color, by amount: Double) -> Color {
        let t = CGFloat(min(1, max(0, amount)))
        var r1: CGFloat = 0, g1: CGFloat = 0, b1: CGFloat = 0, a1: CGFloat = 0
        var r2: CGFloat = 0, g2: CGFloat = 0, b2: CGFloat = 0, a2: CGFloat = 0
        UIColor(self).getRed(&r1, green: &g1, blue: &b1, alpha: &a1)
        UIColor(other).getRed(&r2, green: &g2, blue: &b2, alpha: &a2)
        return Color(.sRGB,
                     red: Double(r1 + (r2 - r1) * t),
                     green: Double(g1 + (g2 - g1) * t),
                     blue: Double(b1 + (b2 - b1) * t),
                     opacity: Double(a1 + (a2 - a1) * t))
    }

}

// ---------------------------------------------------------------- formatting

/// Time formatted as m:ss.mmm
func formatTime(_ t: Double) -> String {
    guard t.isFinite && t > 0 else { return "--:--" }
    let m = Int(t) / 60
    let s = Int(t) % 60
    let ms = Int((t - floor(t)) * 1000)
    return m > 0 ? String(format: "%d:%02d.%03d", m, s, ms)
                 : String(format: "%d.%03d", s, ms)
}

/// A signed lap delta, e.g. "-1.204".
func formatDelta(_ d: Double) -> String {
    let sign = d < 0 ? "-" : "+"
    return sign + String(format: "%.3f", abs(d))
}

/// The suffix an ordinal takes in the device's language - "3rd" in English,
/// "3." in Czech. Taken from the system formatter and worked out once for the
/// range of positions a race can produce, because the HUD sets the digits and
/// the suffix at different sizes and so needs the two apart.
private let ordinalSuffixes: [String] = {
    let f = NumberFormatter()
    f.numberStyle = .ordinal
    return (0...20).map { n in
        guard let s = f.string(from: NSNumber(value: n)) else { return "" }
        return String(s.drop(while: { $0.isNumber }))
    }
}()

func ordinalSuffix(_ n: Int) -> String {
    ordinalSuffixes.indices.contains(n) ? ordinalSuffixes[n] : ""
}
