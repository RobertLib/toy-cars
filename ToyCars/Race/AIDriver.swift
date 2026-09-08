//
//  AIDriver.swift
//  ToyCars
//
//  Driver for the computer-controlled cars. Follows the racing line already
//  computed in Blender, brakes according to curvature, avoids opponents,
//  collects fuel and makes the occasional mistake so it does not feel robotic.
//

import Foundation
import simd

final class AIDriver {
    /// 0 = slow Sunday driver, 1 = tough opponent
    let skill: Float
    private var noisePhase: Float
    private var noiseSpeed: Float
    private var mistakeTimer: Float = 0
    private var mistakeSteer: Float = 0
    private var targetCan: Int? = nil
    private var canCooldown: Float = 0
    private var recover: Float = 0
    /// Every random number this driver draws, from its own stream. Sharing
    /// the global generator instead is what made two identical races come out
    /// differently - a headless race could not be replayed, and the seed
    /// `AIDriverTests` passes in did nothing at all.
    private var rng: SplitMix

    init(skill: Float, seed: Int) {
        self.skill = skill
        rng = SplitMix(seed: UInt64(bitPattern: Int64(seed &* 7919 &+ 13)))
        noisePhase = rng.nextFloat() * 6.28
        noiseSpeed = 0.35 + rng.nextFloat() * 0.5
    }

    func drive(car: CarSim, track: TrackData, others: [CarSim],
               pickups: [PickupState], rubber: Float, dt: Float) {
        noisePhase += dt * noiseSpeed
        canCooldown = max(0, canCooldown - dt)

        let sp = car.speed
        // how far ahead the AI looks - grows with speed
        let look = max(7.0, min(26.0, sp * 0.95 + 6.0))
        let steps = max(4, Int(look / track.step))
        let ahead = track.wrap(car.proj.index + steps)
        let near = track.wrap(car.proj.index + steps / 2)

        // --- desired lateral position: the racing line
        var targetLat = track.racingLine(ahead) * (0.55 + 0.45 * skill)

        // --- fuel
        //
        // A tank is worth about a lap and the races are two or three, so a
        // driver who only starts looking for a canister when the gauge is
        // half down has already lost the race. Waiting for 55 % left whole
        // cars stranded on the last lap - a results table with three DNFs in
        // it, which reads as the game being broken rather than as a hard
        // race. The car now tops up from the moment the tank is off full;
        // what changes with the level in it is how far out of its way it will
        // go. Nearly full, it takes only what is on its line; nearly empty,
        // it will cross the track for one.
        let thirst = max(0, min(1, 1 - car.fuel / (car.def.fuelCapacity * 0.8)))
        let full = car.fuel >= car.def.fuelCapacity * 0.95

        // Let go of a target that has been taken, passed or left behind.
        if let ci = targetCan {
            let gone = ci >= pickups.count || !pickups[ci].active
            let ds = gone ? 0 : forwardDistance(from: car.proj.s,
                                                to: pickups[ci].s,
                                                length: track.length)
            if gone || ds < -3 || ds > 95 { targetCan = nil }
        }
        // Choose a new one only when there is none. Re-deciding on every
        // frame - which is 120 times a second - meant that whenever two
        // canisters scored alike the winner flipped as the car moved, so it
        // weaved between the pair and collected neither. Cars finished races
        // having picked up nothing at all and coasted to a stop on the last
        // lap. Pick one, then go and get it.
        if targetCan == nil && !full {
            targetCan = pickCanister(car: car, track: track,
                                     pickups: pickups, thirst: thirst)
        }
        if full { targetCan = nil }

        if let ci = targetCan, ci < pickups.count, pickups[ci].active {
            let ds = forwardDistance(from: car.proj.s, to: pickups[ci].s,
                                     length: track.length)
            if ds < 55 && ds > -3 {
                let blend = 1 - smoothstep(6, 45, ds)
                targetLat += (pickups[ci].lat - targetLat)
                    * blend * (0.45 + 0.5 * thirst)
            }
        }

        // --- avoidance: do not rear-end the car in front
        var avoid: Float = 0
        for o in others where o !== car {
            let ds = forwardDistance(from: car.proj.s, to: o.proj.s,
                                     length: track.length)
            guard ds > -1.5, ds < 16 else { continue }
            let dl = o.proj.lateral - car.proj.lateral
            if abs(dl) < 2.6 {
                let urgency = (1 - ds / 16)
                avoid += (dl > 0 ? -1 : 1) * urgency * 2.6
            }
        }
        targetLat += max(-4.0, min(4.0, avoid))

        // --- the occasional mistake
        mistakeTimer -= dt
        if mistakeTimer <= 0 {
            mistakeTimer = 2.5 + rng.nextFloat() * 6 * (1.4 - skill)
            mistakeSteer = (rng.nextFloat() * 2 - 1) * (1 - skill) * 0.55
        }
        targetLat += sin(noisePhase) * (1.4 - skill) * 1.5 + mistakeSteer

        let hw = track.halfWidth(ahead)
        targetLat = max(-hw * 0.94, min(hw * 0.94, targetLat))

        // --- steer towards the aim point
        var aim = track.point(ahead, lat: targetLat)
        // Close in, aim at the canister itself rather than at a point on the
        // centreline carrying its lateral offset. The look-ahead point is 20
        // to 26 m down the road, so lining the offset up there steers the car
        // past a canister that is 8 m away - which is why the field used to
        // drive over the fuel it needed and coast to a stop on the last lap.
        // The pickup radius is only 2.6 m; it has to be aimed at.
        if let ci = targetCan, ci < pickups.count, pickups[ci].active {
            let ds = forwardDistance(from: car.proj.s, to: pickups[ci].s,
                                     length: track.length)
            if ds > -2, ds < 32 {
                let pull = (1 - smoothstep(10, 32, ds)) * (0.5 + 0.5 * thirst)
                aim += (pickups[ci].pos - aim) * pull
            }
        }
        let toAim = SIMD2(aim.x - car.pos.x, aim.z - car.pos.z)
        let want = signedAngle(from: car.forward, to: simd_normalize(toAim))
        var steer = max(-1, min(1, want * 2.2))

        // if the car is sliding, apply opposite lock
        if car.slip > 0.35 {
            let drift = signedAngle(from: simd_normalize(car.vel), to: car.forward)
            steer -= max(-0.6, min(0.6, drift * 1.1))
        }

        // --- cornering speed
        let curveAhead = maxCurvature(track: track, from: car.proj.index,
                                      count: steps + 6)
        let latAccel = (13.5 + 8.0 * skill) * track.surface(near).grip
                        * car.def.grip
        var targetSpeed = curveAhead > 0.0025
            ? sqrt(latAccel / curveAhead)
            : car.def.topSpeed * 2
        targetSpeed = min(targetSpeed, car.def.topSpeed * (0.80 + 0.20 * skill))
        targetSpeed *= rubber
        if track.surface(near) == .ice { targetSpeed *= 0.86 }

        // off track or after a crash - get back on the track
        if abs(car.proj.lateral) > hw + 0.6 || car.proj.forward < -0.4 {
            recover = 0.9
        }
        recover = max(0, recover - dt)
        if recover > 0 { targetSpeed = min(targetSpeed, 14) }

        var throttle: Float = 1
        if sp > targetSpeed + 1.5 {
            throttle = -1
        } else if sp > targetSpeed {
            throttle = 0.1
        }
        // Down to the last quarter of a tank with nothing to refuel at: lift
        // off and eke it out. Consumption is charged per second against
        // throttle, so a part-throttle lap costs roughly a third of a flat
        // one - the difference between limping home a few seconds down and
        // stopping dead on the road, which is what the results table used to
        // show. A canister in reach is always worth chasing at full throttle.
        if car.fuel < car.def.fuelCapacity * 0.25 && car.fuel > 0 {
            let chasing = targetCan.map { ci -> Bool in
                guard ci < pickups.count, pickups[ci].active else { return false }
                let ds = forwardDistance(from: car.proj.s, to: pickups[ci].s,
                                         length: track.length)
                return ds > -3 && ds < 50
            } ?? false
            if !chasing { throttle = min(throttle, 0.45) }
        }
        if car.fuel <= 0 { throttle = 0 }

        // handbrake into tight corners - the AI can drift too
        let handbrake = skill > 0.55 && curveAhead > 0.055 && sp > targetSpeed * 1.25

        car.input = DriverInput(throttle: throttle,
                                steer: max(-1, min(1, steer)),
                                handbrake: handbrake)
    }

    /// `thirst` runs 0 (tank full) to 1 (dry) and prices the detour: a full
    /// car will not give up much of the racing line for a canister, an empty
    /// one will give up all of it.
    private func pickCanister(car: CarSim, track: TrackData,
                              pickups: [PickupState], thirst: Float) -> Int? {
        var best: Int? = nil
        var bestScore = Float.greatestFiniteMagnitude
        let lateralCost = 7.0 - 5.5 * thirst
        for (i, p) in pickups.enumerated() where p.active {
            let ds = forwardDistance(from: car.proj.s, to: p.s,
                                     length: track.length)
            guard ds > 4, ds < 90 else { continue }
            let cost = ds + abs(p.lat - car.proj.lateral) * lateralCost
            if cost < bestScore { bestScore = cost; best = i }
        }
        return best
    }

    private func maxCurvature(track: TrackData, from: Int, count: Int) -> Float {
        var m: Float = 0
        var i = from
        for k in 0..<count {
            let w: Float = 1.0 - Float(k) / Float(count) * 0.35
            m = max(m, abs(track.curvature(i)) * w)
            i += 1
        }
        return m
    }
}

@inline(__always) func forwardDistance(from a: Float, to b: Float,
                                       length: Float) -> Float {
    var d = b - a
    if d > length * 0.5 { d -= length }
    if d < -length * 0.5 { d += length }
    return d
}

/// Small deterministic generator so AI behaviour stays reproducible: given
/// the same seed, the same field and the same inputs, a race replays exactly.
/// `AIDriver` draws every one of its random numbers from this and none from
/// the global generator - that is what the promise rests on.
struct SplitMix {
    var state: UInt64
    init(seed: UInt64) { state = seed &+ 0x9E3779B97F4A7C15 }
    mutating func next() -> UInt64 {
        state = state &+ 0x9E3779B97F4A7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58476D1CE4E5B9
        z = (z ^ (z >> 27)) &* 0x94D049BB133111EB
        return z ^ (z >> 31)
    }
    mutating func nextFloat() -> Float {
        Float(next() >> 40) / Float(1 << 24)
    }
}
