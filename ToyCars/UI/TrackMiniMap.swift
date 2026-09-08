//
//  TrackMiniMap.swift
//  ToyCars
//
//  The track drawn straight from the game data - the same polyline the physics
//  drives on, so the map cannot disagree with the circuit.
//
//  Two jobs, two styles. `.map` is the poster on the track select screen: a
//  wide ribbon with a kerb, a dashed centre line and a chequered start gate,
//  laid on the ground with a shadow. `.hud` is the live map during a race,
//  where the only questions are "where am I", "which way is the corner" and
//  "where is the nearest can", so it is thin, bright and uncluttered.
//

import SwiftUI
import simd

@MainActor
final class TrackShapeCache {
    static let shared = TrackShapeCache()
    private var cache: [String: TrackData] = [:]

    func data(_ id: String) -> TrackData? {
        if let d = cache[id] { return d }
        guard let d = TrackData.load(id: id) else { return nil }
        cache[id] = d
        return d
    }
}

struct TrackMiniMap: View {
    let track: TrackData
    var lineWidth: CGFloat = 9
    var roadColor: Color = .white
    var edgeColor: Color = TC.ink.opacity(0.35)
    var style: Style = .hud
    /// Car dots: (position, colour, is the player)
    var cars: [(SIMD3<Float>, Color, Bool)] = []
    /// Active canisters - they show the player where to go for fuel.
    var cans: [SIMD3<Float>] = []
    var startS: Float? = nil

    enum Style { case map, hud }

    var body: some View {
        GeometryReader { geo in
            let m = mapper(size: geo.size)
            let path = shape(m)
            ZStack {
                if style == .map {
                    // The ribbon, built up like a real road: a shadow on the
                    // ground, the verge, the tarmac, then the markings.
                    path.stroke(TC.ink.opacity(0.28), style: round(lineWidth
                                                                   + 9))
                        .offset(y: 4)
                        .blur(radius: 3)
                    path.stroke(edgeColor, style: round(lineWidth + 7))
                    path.stroke(Color.white.opacity(0.85),
                                style: round(lineWidth + 3.5))
                    path.stroke(LinearGradient(
                        colors: [roadColor.lighter(0.10),
                                 roadColor.darker(0.06)],
                        startPoint: .top, endPoint: .bottom),
                                style: round(lineWidth))
                    path.stroke(Color.white.opacity(0.55),
                                style: StrokeStyle(lineWidth: max(1,
                                                                  lineWidth * 0.10),
                                                   lineCap: .butt,
                                                   dash: [lineWidth * 0.5,
                                                          lineWidth * 0.7]))
                } else {
                    path.stroke(edgeColor, style: round(lineWidth + 3))
                    path.stroke(roadColor, style: round(lineWidth))
                }

                if let ss = startS {
                    startGate(ss, m)
                }

                ForEach(Array(cans.enumerated()), id: \.offset) { _, p in
                    ZStack {
                        Circle().fill(TC.cherry)
                        Circle().strokeBorder(.white.opacity(0.9),
                                              lineWidth: 1)
                    }
                    .frame(width: lineWidth * 0.62, height: lineWidth * 0.62)
                    .position(m.point(p))
                }

                ForEach(Array(cars.enumerated()), id: \.offset) { _, c in
                    CarDot(color: c.1, isPlayer: c.2,
                           size: c.2 ? lineWidth * 1.35 : lineWidth * 0.9)
                        .position(m.point(c.0))
                }
            }
        }
    }

    private func round(_ w: CGFloat) -> StrokeStyle {
        StrokeStyle(lineWidth: w, lineCap: .round, lineJoin: .round)
    }

    /// Maps world coordinates into the preview area.
    struct Mapper {
        var ox: CGFloat, oz: CGFloat, s: CGFloat
        var minX: Float, minZ: Float
        func point(_ p: SIMD3<Float>) -> CGPoint {
            CGPoint(x: ox + CGFloat(p.x - minX) * s,
                    y: oz + CGFloat(p.z - minZ) * s)
        }
    }

    private func mapper(size: CGSize) -> Mapper {
        let b = track.extent
        let pad = lineWidth + (style == .map ? 10 : 4)
        let sx = (size.width - pad * 2) / CGFloat(b.width)
        let sz = (size.height - pad * 2) / CGFloat(b.height)
        let s = max(0.01, min(sx, sz))
        return Mapper(ox: (size.width - CGFloat(b.width) * s) / 2,
                      oz: (size.height - CGFloat(b.height) * s) / 2,
                      s: s, minX: b.minX, minZ: b.minZ)
    }

    private func shape(_ m: Mapper) -> Path {
        var path = Path()
        let step = max(1, track.count / 280)
        var i = 0
        var first = true
        while i < track.count {
            let pt = m.point(track.positions[i])
            if first { path.move(to: pt); first = false } else {
                path.addLine(to: pt)
            }
            i += step
        }
        path.closeSubpath()
        return path
    }

    /// The start line: a chequered band laid across the road, square to it.
    private func startGate(_ ss: Float, _ m: Mapper) -> some View {
        let i = track.index(atS: ss)
        let w = lineWidth * (style == .map ? 1.9 : 1.6)
        let t = style == .map ? lineWidth * 0.60 : 3.5
        return Group {
            if style == .map {
                CheckerStrip(squares: 2, cell: t / 2,
                             light: .white, dark: TC.ink)
                    .frame(width: w, height: t)
                    .clipShape(RoundedRectangle(cornerRadius: 1))
                    .overlay(RoundedRectangle(cornerRadius: 1)
                        .strokeBorder(TC.ink.opacity(0.5), lineWidth: 0.6))
                    .rotationEffect(.radians(Double(-track.heading(i))))
                    .position(m.point(track.positions[i]))
                    .shadow(color: TC.contact, radius: 2, y: 1)
            } else {
                Rectangle()
                    .fill(.white)
                    .frame(width: w, height: t)
                    .overlay(Rectangle().fill(TC.ink.opacity(0.75))
                        .frame(height: t / 2)
                        .offset(y: -t / 4))
                    .rotationEffect(.radians(Double(-track.heading(i))))
                    .position(m.point(track.positions[i]))
            }
        }
    }
}

/// A car on the map. The player's dot gets a white ring and a halo so it can
/// be picked out of a pack without looking twice.
///
/// The halo is two flat rings rather than a blurred disc, and there is no
/// drop shadow on the dots at all. This view is drawn eight times inside the
/// HUD map, which redraws every simulation frame, and the HUD's one standing
/// rule is that nothing in it costs an offscreen pass - a blur and eight
/// shadows are exactly that.
struct CarDot: View {
    var color: Color
    var isPlayer: Bool
    var size: CGFloat

    var body: some View {
        ZStack {
            if isPlayer {
                Circle()
                    .fill(color.opacity(0.28))
                    .frame(width: size * 1.9, height: size * 1.9)
                Circle()
                    .fill(color.opacity(0.32))
                    .frame(width: size * 1.45, height: size * 1.45)
            }
            Circle()
                .fill(LinearGradient(colors: [color.lighter(0.25), color],
                                     startPoint: .top, endPoint: .bottom))
            // Stands in for the contact shadow: a dark keyline separates the
            // dot from the road under it for nothing.
            Circle().strokeBorder(TC.ink.opacity(0.45),
                                  lineWidth: isPlayer ? 3 : 2)
            Circle().strokeBorder(.white.opacity(isPlayer ? 1 : 0.75),
                                  lineWidth: isPlayer ? 2 : 1)
        }
        .frame(width: size, height: size)
    }
}
