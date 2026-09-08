//
//  AIDriverTests.swift
//  ToyCarsTests
//
//  Whole races run headlessly, because the thing that was wrong with the AI
//  could not be seen in a second of driving - only in who was still moving
//  after two laps.
//

import Foundation
import Testing
import simd
@testable import ToyCars

@MainActor
struct AIDriverTests {

    /// One full race on Sunset with the player's car driven by the AI too,
    /// so all eight are under test. Returns how many failed to finish.
    private func race(seed: Int) throws -> Int {
        let track = try #require(TrackData.load(id: "sunset"))
        let player = try #require(CarCatalog.shared.car("bumble"))
        let opponents = RaceController().pickOpponents(player: player,
                                                       count: 7, difficulty: 0)
        let e = RaceEngine(track: track, playerCar: player,
                           opponents: opponents, difficulty: 0)
        let auto = AIDriver(skill: 0.85, seed: seed)
        let frame = 1.0 / 60.0
        var t = 0.0
        while t < 220 {
            if e.phase == .racing || e.phase == .finished {
                auto.drive(car: e.playerCar, track: e.track, others: e.cars,
                           pickups: e.pickups, rubber: 1.0, dt: Float(frame))
            }
            e.update(dt: frame)
            t += frame
        }
        return e.cars.filter { !$0.finished }.count
    }

    /// The regression this file exists for.
    ///
    /// A tank is worth about a lap and a race is two, so every car has to
    /// collect two or three canisters on the way round. They were not
    /// collecting them: the AI re-chose its target 120 times a second, so
    /// whenever two canisters scored alike the winner flipped as the car
    /// moved and it weaved between the pair, and even when it did commit it
    /// steered at a point on the centreline 20-odd metres ahead carrying the
    /// canister's lateral offset rather than at the canister, whose pickup
    /// radius is 2.6 m. Cars finished races having collected nothing and
    /// coasted to a stop on the last lap; this same harness recorded 33
    /// retirements over 20 races, better than one in five cars.
    ///
    /// It is a stochastic test - the AI makes deliberate random mistakes - so
    /// the bar is set well clear of where it now sits (about one retirement
    /// in four races) and well under where it was (about eight in four).
    @Test func theFieldDoesNotRunOutOfFuel() throws {
        var dnf = 0
        for seed in 0..<4 { dnf += try race(seed: seed * 17 + 3) }
        #expect(dnf <= 3,
                "\(dnf) cars in 4 races retired; the field is not refuelling")
    }

}
