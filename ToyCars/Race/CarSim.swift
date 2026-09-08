//
//  CarSim.swift
//  ToyCars
//
//  Arcade driving model. Deliberately not a realistic simulation - the goal
//  is one-finger control, satisfying drifts and forgiving mistakes.
//

import Foundation
import simd

struct DriverInput: Sendable {
    var throttle: Float = 0      // -1 brake/reverse .. 1 throttle
    var steer: Float = 0         // -1 left .. 1 right
    var handbrake: Bool = false
}

final class CarSim {
    /// Toy-scale gravity: heavier than the real thing so jumps land quickly
    /// and a climb is felt at once. Used for airtime and for hills alike.
    static let gravity: Float = 24.0

    /// Closing speed above which a knock counts as an impact rather than as
    /// traffic - it stuns, flashes and is what the crash sound is graded from.
    static let hardBump: Float = 7.0

    let def: CarDef
    let slot: Int
    let isPlayer: Bool

    // ------------------------------------------------------------ state
    var pos: SIMD3<Float> = .zero
    var heading: Float = 0          // 0 = facing +Z
    var vel: SIMD2<Float> = .zero   // XZ plane
    var vy: Float = 0
    var airborne = false
    var input = DriverInput()

    var fuel: Float
    var boost: Float = 0            // remaining boost time
    var stunned: Float = 0          // after a hard impact
    var slip: Float = 0             // 0..1 amount of slide
    var wheelSpin: Float = 0        // wheel rotation angle
    var steerAngle: Float = 0       // visual steering angle of the front axle
    var bodyRoll: Float = 0
    var bodyPitch: Float = 0
    /// Lean that comes from the road itself, not from the driving.
    var groundPitch: Float = 0

    // ------------------------------------------------------------ race
    var proj: TrackData.Projection
    var lap: Int = 0
    var lapStartTime: Double = 0
    var lapTimes: [Double] = []
    var bestLap: Double = .infinity
    var passedHalf = false
    var finished = false
    var finishTime: Double = 0
    var position: Int = 1
    var outOfFuelSince: Double? = nil
    var lastRampSlope: Float = 0
    var offTrack = false
    var surface: Surface = .asphalt
    var landedHard: Float = 0
    var bumpFlash: Float = 0
    /// The boost pad this car is currently standing on, so it fires once per
    /// visit instead of once per second the car spends parked on it.
    var lastBoostPad: Int = -1
    /// 0..1 - how much the car is drafting behind the one ahead.
    var draft: Float = 0

    /// Distance covered measured from the finish line, so that `progress` is
    /// directly comparable between cars: 0 is the line, one lap done is
    /// exactly `track.length`, and the race is over at `laps * length`.
    ///
    /// It is accumulated frame by frame rather than read off `proj.s`, so
    /// reversing or crossing the line backwards cannot skew the order - and
    /// it *starts negative*, at minus the distance from the car's grid slot
    /// to the line. That offset is the whole point. Starting every car at
    /// zero instead measured distance driven rather than distance made good,
    /// which handed the back row the 19.6 m of grid it still had to make up:
    /// two cars level on the road came out 19.6 m apart in the standings, the
    /// back row was shown leading before the lights went out, and a car
    /// genuinely third was reported fifth. Everything that ranks the field
    /// reads this - the position plate, the rubber-banding, and the order the
    /// results table puts the non-finishers in.
    private(set) var progress: Float = 0
    private var lastS: Float = 0
    private let trackLength: Float

    // ------------------------------------------------------------ AI
    var ai: AIDriver?

    init(def: CarDef, slot: Int, isPlayer: Bool, track: TrackData,
         start: SIMD3<Float>, heading: Float) {
        self.def = def
        self.slot = slot
        self.isPlayer = isPlayer
        self.fuel = def.fuelCapacity
        self.trackLength = track.length
        self.pos = start
        self.heading = heading
        self.proj = track.projectGlobal(start)
        self.lastS = proj.s
        // The grid sits behind the line, so this is negative: the car has to
        // drive that far before it has made any ground at all.
        self.progress = -forwardDistance(from: proj.s, to: track.file.startS,
                                         length: track.length)
    }

    @inline(__always) var forward: SIMD2<Float> {
        SIMD2(sin(heading), cos(heading))
    }
    /// The driver's right-hand direction. r = cross(forward, up), so for
    /// forward = (sin h, cos h) this gives (-cos h, sin h).
    @inline(__always) var right: SIMD2<Float> {
        SIMD2(-cos(heading), sin(heading))
    }
    var speed: Float { simd_length(vel) }
    var speedKph: Float { speed * 3.6 * 1.9 }   // "toy" speedometer

    // ------------------------------------------------------------ step

    func step(dt: Float, track: TrackData, racing: Bool) {
        // --- project onto the track
        proj = track.project(pos, hint: proj.index)
        let ds = forwardDistance(from: lastS, to: proj.s, length: trackLength)
        if abs(ds) < trackLength * 0.25 { progress += ds }
        lastS = proj.s
        let hw = track.halfWidth(proj.index)
        offTrack = abs(proj.lateral) > hw
        surface = offTrack ? offSurface(track) : track.surface(proj.index)

        var gripMul = surface.grip * def.grip
        var rollMul = surface.rolling
        if offTrack {
            let over = min(1, (abs(proj.lateral) - hw) / 3.0)
            gripMul *= (1.0 - 0.30 * over)
            rollMul *= (1.0 - 0.34 * over)
        }

        // --- inputs
        var thr = racing ? input.throttle : 0
        var str = racing ? input.steer : 0
        if fuel <= 0 { thr = min(thr, 0) }
        if stunned > 0 {
            stunned -= dt
            thr *= 0.15
            str *= 0.4
        }
        if airborne { thr *= 0.15 }

        // --- steering
        let sp = simd_length(vel)
        let along = simd_dot(vel, forward)
        let ratio = min(1, sp / max(4, def.topSpeed))
        // at low speed there is nothing to steer with, at high speed it calms down
        let authority = smoothstep(0.0, 4.0, sp) * (1.0 - 0.42 * ratio)
        let airMul: Float = airborne ? 0.30 : 1.0
        var yawRate = str * def.maxYawRate * authority * airMul
        yawRate *= (0.72 + 0.28 * min(1.4, gripMul))
        if along < -0.5 { yawRate = -yawRate }   // reverse
        // A growing heading turns the car left, so a positive input
        // (= right) is subtracted.
        let fwd0 = forward
        heading -= yawRate * dt
        // A car turns around its rear axle, not around its middle: the front
        // wheels point the way and the back end follows. Rotating the body
        // about its origin instead sweeps the whole tail out through every
        // corner, which reads as if the car swivelled on its rear bumper.
        // Shifting the pivot back to the rear axle is a single extra offset.
        // In a slide the pivot creeps forward again, so the tail steps out
        // when - and only when - the player is actually drifting.
        let loose = max(slip, input.handbrake ? 0.85 : 0)
        let pivotBack = airborne ? 0 : def.wheelbase * 0.5 * (1 - 0.85 * loose)
        let swing = (forward - fwd0) * pivotBack
        pos.x += swing.x
        pos.z += swing.y
        steerAngle += (str * 0.52 - steerAngle) * min(1, dt * 12)

        // --- grip: velocity is pulled towards the car's facing direction
        if sp > 0.06 {
            let vdir = vel / sp
            let ang = signedAngle(from: vdir, to: forward)
            var rate = (input.handbrake ? def.driftGrip : def.gripBase) * gripMul
            if airborne { rate *= 0.20 }
            if boost > 0 { rate *= 1.12 }
            let maxTurn = rate * dt
            let turn = max(-maxTurn, min(maxTurn, ang))
            vel = rotate(vdir, by: turn) * sp
            slip = min(1, abs(ang) / 0.9)
        } else {
            slip = 0
        }

        // --- longitudinal forces
        let topSpeed = def.topSpeed * rollMul * (boost > 0 ? 1.34 : 1.0)
        var acc: Float = 0
        if thr > 0 {
            let head = max(0, 1 - along / topSpeed)
            acc = def.acceleration * thr * head * (boost > 0 ? 1.9 : 1.0)
            acc *= (1 + 0.16 * draft)
            acc *= (0.62 + 0.38 * min(1.2, surface.grip))
        } else if thr < 0 {
            acc = along > 0.5 ? -def.acceleration * 1.55 : def.acceleration * 0.55 * thr
        }
        if input.handbrake { acc -= def.acceleration * 0.45 }
        vel += forward * acc * dt

        // --- gravity along the road
        // Climbing costs speed and dropping gains it - the whole point of a
        // track with hills. A car standing still on the throttle-off holds
        // its place instead: otherwise the grid would roll away during the
        // countdown and a car out of fuel would coast downhill for ever.
        let tdir = track.tangents[proj.index]
        let roadSlope = track.slope(proj.index) + lastRampSlope
        let noseUp = roadSlope * simd_dot(forward, tdir)
        if racing && !airborne && !(abs(along) < 0.8 && thr <= 0) {
            vel -= forward * (CarSim.gravity * noseUp) * dt
        }

        // --- resistance
        // drafting means less air resistance - you can reel an opponent in
        let drag: Float = (0.0010 * (1 - 0.42 * draft)) + (offTrack ? 0.0050 : 0)
        vel -= vel * simd_length(vel) * drag * dt
        let roll: Float = 0.55 + (offTrack ? 4.2 : 0)
        vel -= vel * roll * dt * 0.10
        // lateral scrub - sliding costs speed
        let lat = simd_dot(vel, right)
        let scrub: Float = input.handbrake ? 0.9 : 2.1
        vel -= right * (lat * scrub * dt)

        if boost > 0 { boost -= dt }

        // --- translation
        pos.x += vel.x * dt
        pos.z += vel.y * dt

        // --- height, ramps, airtime
        let ramp = track.rampHeight(atS: proj.s)
        let groundY = proj.height + ramp.height
        if airborne {
            vy -= CarSim.gravity * dt
            pos.y += vy * dt
            if pos.y <= groundY {
                landedHard = min(1, max(0, -vy / 9))
                pos.y = groundY
                vy = 0
                airborne = false
                vel *= (1.0 - 0.16 * landedHard)
            }
        } else {
            // On the ground the car follows the road exactly. The elevation
            // profile is smooth, so there is nothing to filter - and easing
            // towards it would leave the car lagging below every climb and
            // hovering over every descent.
            pos.y = groundY
            vy = 0
            // Two ways to leave the ground. A ramp ends at its steepest
            // point, so the moment the car runs out of ramp it carries on
            // along the lip. And a crest lets go when the road falls away
            // faster than gravity can pull the car down after it, which is
            // exactly when v^2 times the profile's curvature beats g.
            let vAlong = simd_dot(vel, tdir)
            let leftRamp = lastRampSlope > 0.06 && ramp.slope <= 0
            let crest = -track.slopeRate(proj.index) * vAlong * vAlong
            if abs(vAlong) > 5 && (leftRamp || crest > CarSim.gravity * 0.85) {
                airborne = true
                vy = max(0, min(11.0, vAlong * roadSlope))
            }
        }
        lastRampSlope = ramp.slope

        // --- barriers
        let limit = hw + track.wallMargin
        if abs(proj.lateral) > limit {
            let sgn: Float = proj.lateral > 0 ? 1 : -1
            let n = track.normals[proj.index] * sgn
            let over = abs(proj.lateral) - limit
            pos.x -= n.x * over
            pos.z -= n.y * over
            let vn = simd_dot(vel, n)
            if vn > 0 {
                vel -= n * vn * 1.55
                if vn > 6 { stunned = max(stunned, 0.22); bumpFlash = 1 }
            }
            vel *= 0.90
            proj.lateral = sgn * limit
        }

        // --- fuel
        if racing && !finished {
            let load = 0.26 + max(0, thr) * (0.74 + (boost > 0 ? 0.6 : 0))
            fuel = max(0, fuel - load * def.fuelBurn * dt)
        }

        // --- visual body lean
        let targetRoll = simd_dot(vel, right) * 0.020
        bodyRoll += (max(-0.16, min(0.16, targetRoll)) - bodyRoll) * min(1, dt * 8)
        let targetPitch = -acc * 0.0055 + (airborne ? -vy * 0.012 : 0)
        bodyPitch += (max(-0.13, min(0.13, targetPitch)) - bodyPitch) * min(1, dt * 7)
        // The body has to follow the road as well, or a car on a 15 % climb
        // reads as flying up the hill with its wheels off the ground.
        let wantGround = airborne ? 0 : -atan(noseUp)
        groundPitch += (wantGround - groundPitch)
            * min(1, dt * (airborne ? 5 : 14))
        wheelSpin += (along / max(0.05, def.wheelR)) * dt
        if landedHard > 0 { landedHard = max(0, landedHard - dt * 2) }
        if bumpFlash > 0 { bumpFlash = max(0, bumpFlash - dt * 2.5) }
    }

    private func offSurface(_ track: TrackData) -> Surface {
        switch track.surface(proj.index) {
        case .snow, .ice: return .snow
        case .sand, .wood: return .sand
        default: return .dirt
        }
    }

    /// Bounce off another car - bumping is fun, not a punishment.
    ///
    /// Returns the closing speed of a hard impact, or 0 if the cars only
    /// brushed: the sound and the haptic are graded by it.
    ///
    /// A car is a capsule lying along its own heading, not a circle. Testing
    /// the distance between two *centres* against half the two widths made
    /// every car as long as it is wide: nose to tail they only touched once
    /// their centres were 1.8 m apart, which on a 4 m car leaves a metre of
    /// boot inside the other one's bonnet. Cars visibly drove through each
    /// other in a queue. The segment spans the axle ends and the radius is
    /// half the width, so the two together come to the car's real length and
    /// a rear-end shunt happens where the bumpers are.
    @discardableResult
    func bump(with other: CarSim, dt: Float) -> Float {
        let c1 = SIMD2(pos.x, pos.z)
        let c2 = SIMD2(other.pos.x, other.pos.z)
        let f1 = forward * max(0, (def.length - def.width) * 0.5)
        let f2 = other.forward
            * max(0, (other.def.length - other.def.width) * 0.5)
        let (a, b) = closestPointsOnSegments(c1 - f1, c1 + f1, c2 - f2, c2 + f2)
        let d = b - a
        let dist = simd_length(d)
        let minDist = (def.width + other.def.width) * 0.52
        guard dist < minDist else { return 0 }
        // Two segments can cross outright - a T-bone - and then the closest
        // points coincide and there is no direction in them. The line of
        // centres is the one that still separates the cars.
        let n: SIMD2<Float>
        if dist > 0.0001 {
            n = d / dist
        } else {
            let dc = c2 - c1
            let l = simd_length(dc)
            n = l > 0.0001 ? dc / l : forward
        }
        let overlap = minDist - dist
        let m1 = def.mass, m2 = other.def.mass
        let total = m1 + m2
        pos.x -= n.x * overlap * (m2 / total)
        pos.z -= n.y * overlap * (m2 / total)
        other.pos.x += n.x * overlap * (m1 / total)
        other.pos.z += n.y * overlap * (m1 / total)

        let rel = simd_dot(vel - other.vel, n)
        guard rel > 0 else { return 0 }
        let j = rel * 1.35
        vel -= n * j * (m2 / total)
        other.vel += n * j * (m1 / total)
        guard rel > CarSim.hardBump else { return 0 }
        stunned = max(stunned, 0.12)
        other.stunned = max(other.stunned, 0.12)
        bumpFlash = 1
        other.bumpFlash = 1
        return rel
    }
}

// ------------------------------------------------------------------ helpers

@inline(__always) func smoothstep(_ a: Float, _ b: Float, _ x: Float) -> Float {
    let t = max(0, min(1, (x - a) / max(1e-6, b - a)))
    return t * t * (3 - 2 * t)
}

@inline(__always) func signedAngle(from a: SIMD2<Float>,
                                   to b: SIMD2<Float>) -> Float {
    let cross = a.x * b.y - a.y * b.x
    let dot = max(-1, min(1, a.x * b.x + a.y * b.y))
    return atan2(cross, dot)
}

@inline(__always) func rotate(_ v: SIMD2<Float>, by a: Float) -> SIMD2<Float> {
    let c = cos(a), s = sin(a)
    return SIMD2(v.x * c - v.y * s, v.x * s + v.y * c)
}

/// The closest pair of points on two line segments in the plane - the
/// standard clamped-parameter solution, with the degenerate cases (a segment
/// of zero length, two parallel segments) folded in. `CarSim.bump` uses it to
/// measure one car's body against another's.
@inline(__always) func closestPointsOnSegments(
    _ p1: SIMD2<Float>, _ q1: SIMD2<Float>,
    _ p2: SIMD2<Float>, _ q2: SIMD2<Float>
) -> (SIMD2<Float>, SIMD2<Float>) {
    let eps: Float = 1e-6
    let d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2
    let a = simd_dot(d1, d1)        // squared length of segment 1
    let e = simd_dot(d2, d2)        // squared length of segment 2
    let f = simd_dot(d2, r)
    var s: Float = 0, t: Float = 0

    if a <= eps && e <= eps { return (p1, p2) }     // both are points
    if a <= eps {
        t = max(0, min(1, f / e))
    } else {
        let c = simd_dot(d1, r)
        if e <= eps {
            s = max(0, min(1, -c / a))
        } else {
            let b = simd_dot(d1, d2)
            let denom = a * e - b * b                // >= 0, zero if parallel
            s = denom > eps ? max(0, min(1, (b * f - c * e) / denom)) : 0
            t = (b * s + f) / e
            // t fell off its segment, so pin it and solve s again against it
            if t < 0 {
                t = 0
                s = max(0, min(1, -c / a))
            } else if t > 1 {
                t = 1
                s = max(0, min(1, (b - c) / a))
            }
        }
    }
    return (p1 + d1 * s, p2 + d2 * t)
}

@inline(__always) func angleDelta(_ a: Float, _ b: Float) -> Float {
    var d = b - a
    while d > .pi { d -= 2 * .pi }
    while d < -.pi { d += 2 * .pi }
    return d
}
