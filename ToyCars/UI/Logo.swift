//
//  Logo.swift
//  ToyCars
//
//  The wordmark, built as a die-cast badge rather than set as text: a speed
//  swoosh behind it, the two words locked together at opposing angles, a
//  checkered strip under the join and a chrome bar carrying the strapline.
//
//  The angles matter. "TOY" leans back four degrees and "CARS" forward two,
//  which makes the pair read as one moving object; set flat, the same two words
//  read as a caption. Everything is drawn from type and shapes, so the badge is
//  resolution independent and needs no artwork in the bundle.
//

import SwiftUI

struct GameLogo: View {
    /// Height of the "CARS" cap - everything else is derived from it.
    var scale: CGFloat = 70
    var animate: Bool = true

    @State private var pop: CGFloat = 0.72
    @State private var swoosh: CGFloat = -1

    var body: some View {
        VStack(spacing: -scale * 0.16) {
            ZStack {
                swooshLayer
                wordmark
            }
            strapline
                // The offset moves the bar down without taking up layout
                // space, so the padding gives that space back - otherwise
                // whatever sits under the badge overlaps the strapline.
                .offset(y: scale * 0.30)
                .padding(.bottom, scale * 0.30)
        }
        .scaleEffect(pop)
        .onAppear {
            guard animate else { pop = 1; return }
            withAnimation(.spring(response: 0.62, dampingFraction: 0.52)) {
                pop = 1
            }
            withAnimation(.easeOut(duration: 1.1).delay(0.25)) {
                swoosh = 1
            }
        }
    }

    // ------------------------------------------------------------ words

    private var wordmark: some View {
        VStack(spacing: -scale * 0.20) {
            StrokeText(
                text: Text(verbatim: "TOY").font(TC.title(scale * 0.80)),
                width: scale * 0.045,
                color: TC.ink,
                outer: TC.cream,
                outerWidth: scale * 0.030,
                dropShadow: scale * 0.05,
                shadowColor: TC.ink.opacity(0.45))
                .foregroundStyle(TC.metalGradient(TC.gold))
                .rotationEffect(.degrees(-4))
                .zIndex(1)

            ZStack {
                StrokeText(
                    text: Text(verbatim: "CARS").font(TC.title(scale)),
                    width: scale * 0.05,
                    color: TC.cherry.darker(0.18),
                    outer: TC.cream,
                    outerWidth: scale * 0.032,
                    dropShadow: scale * 0.06,
                    shadowColor: TC.ink.opacity(0.45))
                    .foregroundStyle(LinearGradient(
                        stops: [.init(color: .white, location: 0),
                                .init(color: Color(hex: "#ffe9ef"),
                                      location: 0.48),
                                .init(color: Color(hex: "#ffc9d4"),
                                      location: 0.52),
                                .init(color: .white, location: 1)],
                        startPoint: .top, endPoint: .bottom))
                // A single hot highlight travelling across the letters is what
                // sells them as painted metal rather than filled shapes.
                Text(verbatim: "CARS")
                    .font(TC.title(scale))
                    .foregroundStyle(.white)
                    .mask(
                        LinearGradient(
                            stops: [.init(color: .clear, location: 0.0),
                                    .init(color: .white.opacity(0.85),
                                          location: 0.5),
                                    .init(color: .clear, location: 1.0)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing)
                        .frame(width: scale * 1.4)
                        .offset(x: swoosh * scale * 2.6)
                    )
                    .blendMode(.plusLighter)
            }
            .rotationEffect(.degrees(2))

            CheckerStrip(squares: 2, cell: scale * 0.095,
                         light: TC.cream, dark: TC.ink)
                .frame(width: scale * 2.5)
                .clipShape(Capsule())
                .overlay(Capsule().strokeBorder(TC.ink.opacity(0.5),
                                                lineWidth: 1))
                .rotationEffect(.degrees(2))
                .offset(y: scale * 0.06)
                .shadow(color: TC.contact, radius: 3, y: 2)
        }
    }

    /// Three tapered speed streaks fanning out behind the words.
    private var swooshLayer: some View {
        ZStack {
            ForEach(0..<3, id: \.self) { i in
                let t = CGFloat(i)
                Capsule()
                    .fill(LinearGradient(
                        colors: [TC.turbo.opacity(0.0),
                                 TC.sky.opacity(0.55 - t * 0.14),
                                 TC.turbo.opacity(0.0)],
                        startPoint: .leading, endPoint: .trailing))
                    .frame(width: scale * (3.4 - t * 0.5),
                           height: scale * (0.13 - t * 0.02))
                    .offset(x: -scale * 0.1 + t * scale * 0.16,
                            y: -scale * 0.30 + t * scale * 0.42)
                    .rotationEffect(.degrees(-6))
                    .blendMode(.plusLighter)
            }
        }
        .allowsHitTesting(false)
    }

    // ------------------------------------------------------------ strapline

    private var strapline: some View {
        Text("LITTLE CARS · BIG CORNERS")
            .font(TC.label(scale * 0.135))
            .tracking(scale * 0.030)
            .foregroundStyle(TC.ink.opacity(0.72))
            .lineLimit(1)
            .minimumScaleFactor(0.6)
            .padding(.horizontal, scale * 0.26)
            .padding(.vertical, scale * 0.075)
            .background {
                ZStack {
                    Capsule().fill(TC.metalGradient(TC.silver))
                    Sheen(shape: Capsule(), period: 5.4, strength: 0.4)
                    Capsule().strokeBorder(TC.silver.dark.opacity(0.7),
                                           lineWidth: 1)
                }
                .compositingGroup()
                .shadow(color: TC.contact, radius: 4, y: 3)
            }
    }
}

/// A compact one-line version for the loading screen and the pause panel,
/// where the full badge would be too tall.
struct LogoMark: View {
    var scale: CGFloat = 26

    var body: some View {
        HStack(spacing: scale * 0.22) {
            CheckerStrip(squares: 3, cell: scale * 0.26,
                         light: TC.cream, dark: TC.ink)
                .frame(width: scale * 0.78)
                .clipShape(RoundedRectangle(cornerRadius: scale * 0.12,
                                            style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: scale * 0.12,
                                          style: .continuous)
                    .strokeBorder(.white.opacity(0.5), lineWidth: 1))
            HStack(spacing: scale * 0.10) {
                Text(verbatim: "TOY")
                    .font(TC.title(scale))
                    .foregroundStyle(TC.metalGradient(TC.gold))
                Text(verbatim: "CARS")
                    .font(TC.title(scale))
                    .foregroundStyle(.white)
            }
            .hardShadow(scale * 0.06, TC.ink.opacity(0.5))
        }
    }
}
