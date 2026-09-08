//
//  CarCatalog.swift
//  ToyCars
//
//  Car catalog - stats and unlock order.
//

import Foundation
import SwiftUI

struct CarStats: Decodable, Sendable {
    let top: Float
    let accel: Float
    let grip: Float
    let weight: Float
    let fuel: Float
}

struct CarDef: Decodable, Identifiable, Sendable {
    let id: String
    let name: String
    let tagline: String
    let paint: String
    let length: Float
    let width: Float
    let wheelR: Float
    let wheelbase: Float
    let top: Float
    let accel: Float
    let grip: Float
    let weight: Float
    let fuel: Float

    var color: Color { Color(hex: paint) }

    /// The manifest stores the English tagline; the string catalog supplies
    /// the translation for the device language.
    var localizedTagline: String {
        String(localized: String.LocalizationValue(tagline))
    }

    // ------------------------------------------------------- derived stats
    /// Top speed in m/s. The scale is chosen so a lap of a ~1.6 km track
    /// takes around a minute - brisk, but readable.
    var topSpeed: Float { 34.0 * top }
    var acceleration: Float { 18.5 * accel / max(0.6, weight) }
    var gripBase: Float { 4.7 * grip }
    var driftGrip: Float { 1.05 * grip }
    var maxYawRate: Float { 2.25 * (1.9 - weight * 0.55) }
    var mass: Float { 900 * weight }
    var fuelCapacity: Float { 100 * fuel }
    /// Consumption per second at full throttle.
    var fuelBurn: Float { 1.55 * (0.75 + weight * 0.25) }

    /// Prices follow the catalog order.
    static let prices: [String: Int] = [
        "bumble": 0, "chili": 0, "sprout": 350, "frosty": 600,
        "scoop": 850, "bolt": 1400, "rocky": 1150, "neo": 2000,
    ]
    var price: Int { CarDef.prices[id] ?? 500 }
}

// ---------------------------------------------------------------- ratings

extension CarDef {
    /// The stats normalised to 0…1 across the range the catalogue actually
    /// uses, not across the raw multipliers - otherwise every bar would sit
    /// around the middle and tell the player nothing.
    var speedScore: Float { clamp01((top - 0.88) / 0.36) }
    var accelScore: Float { clamp01((accel - 0.90) / 0.32) }
    var gripScore: Float { clamp01((grip - 0.96) / 0.24) }
    var fuelScore: Float { clamp01((fuel - 0.82) / 0.46) }

    /// Weighted overall. Speed and acceleration win races on these tracks, so
    /// they carry more than grip, and the tank is a convenience.
    var overall: Float {
        clamp01(speedScore * 0.34 + accelScore * 0.28
                + gripScore * 0.24 + fuelScore * 0.14)
    }

    var classLetter: String {
        switch overall {
        case 0.80...: return "S"
        case 0.60..<0.80: return "A"
        case 0.40..<0.60: return "B"
        case 0.20..<0.40: return "C"
        default: return "D"
        }
    }

    var classColor: Color {
        switch classLetter {
        case "S": return TC.grape
        case "A": return TC.cherry
        case "B": return TC.tangerine
        case "C": return TC.sky
        default: return TC.steel
        }
    }

    /// One to three stars, for players who would rather not read a letter.
    ///
    /// The class shield coarsened, rather than a second scale of its own, so
    /// the two cannot disagree about the shape of the ladder: D and C are one
    /// star, B is two, A and S are three.
    ///
    /// It used to be `1 + Int(overall * 2.7)`, which is a truncation of a
    /// range the catalogue does not use. `overall` runs from 0.33 to 0.85
    /// here, so `overall * 2.7` runs from 0.89 to 2.29 - and the integer part
    /// of that is 1 for six of the eight cars. Six cars showed exactly two
    /// stars while the shield beside them read C, B, B, B, A, A, and Bolt sat
    /// seven thousandths short of its third star. A rating that cannot tell
    /// the showroom apart is not a rating, and this one had to, because it is
    /// the version for players who are not reading the letter.
    var stars: Int {
        switch classLetter {
        case "S", "A": return 3
        case "B": return 2
        default: return 1
        }
    }

    /// Top speed as the in-race speedometer shows it. `CarSim.speedKph`
    /// exaggerates by 1.9 for a toy-scale dial; the garage has to quote the
    /// same number or the two screens contradict each other.
    var displayTopSpeed: Int { Int(topSpeed * 3.6 * 1.9) }
}

private func clamp01(_ v: Float) -> Float { max(0, min(1, v)) }

@MainActor
final class CarCatalog {
    static let shared = CarCatalog()
    let cars: [CarDef]
    /// The order in which cars are shown in the garage.
    static let order = ["bumble", "chili", "sprout", "frosty",
                        "scoop", "rocky", "bolt", "neo"]

    private init() {
        var loaded: [CarDef] = []
        if let url = Bundle.main.url(forResource: "cars_manifest",
                                     withExtension: "json"),
           let data = try? Data(contentsOf: url),
           let list = try? JSONDecoder().decode([CarDef].self, from: data) {
            loaded = list
        }
        let rank = Dictionary(uniqueKeysWithValues:
                                CarCatalog.order.enumerated().map { ($1, $0) })
        cars = loaded.sorted { (rank[$0.id] ?? 99) < (rank[$1.id] ?? 99) }
    }

    func car(_ id: String) -> CarDef? { cars.first { $0.id == id } }
}

struct TrackDef: Decodable, Identifiable, Sendable {
    let id: String
    let name: String
    let subtitle: String
    let theme: String
    let laps: Int
    let unlock: Int
    let difficulty: Int
    let reward: Int
    let length: Float

    /// The manifest stores the English subtitle; the string catalog supplies
    /// the translation for the device language.
    var localizedSubtitle: String {
        String(localized: String.LocalizationValue(subtitle))
    }
}

@MainActor
final class TrackCatalog {
    static let shared = TrackCatalog()
    let tracks: [TrackDef]

    private init() {
        var loaded: [TrackDef] = []
        if let url = Bundle.main.url(forResource: "tracks_manifest",
                                     withExtension: "json"),
           let data = try? Data(contentsOf: url),
           let list = try? JSONDecoder().decode([TrackDef].self, from: data) {
            loaded = list
        }
        tracks = loaded.sorted { $0.unlock < $1.unlock }
    }

    func track(_ id: String) -> TrackDef? { tracks.first { $0.id == id } }
}

extension Color {
    init(hex: String) {
        var s = hex.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("#") { s.removeFirst() }
        var v: UInt64 = 0
        Scanner(string: s).scanHexInt64(&v)
        let r = Double((v >> 16) & 0xff) / 255
        let g = Double((v >> 8) & 0xff) / 255
        let b = Double(v & 0xff) / 255
        self.init(.sRGB, red: r, green: g, blue: b, opacity: 1)
    }
}
