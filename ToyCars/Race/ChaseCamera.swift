//
//  ChaseCamera.swift
//  ToyCars
//
//  Camera above the track, slightly to the side and behind - the usual setup
//  for top-down racers like Micro Machines, but with smooth yaw that follows
//  the track direction so the player always sees what is coming.
//

import Foundation
import RealityKit
import simd

struct ChaseCamera {
    /// height above the car
    var height: Float = 16.5
    /// distance behind the car
    var distance: Float = 12.0
    /// how far ahead of the car the camera looks
    var lookAhead: Float = 12.0
    var fovDegrees: Float = 50

    private var yaw: Float = 0
    private var pos: SIMD3<Float> = .zero
    private var focus: SIMD3<Float> = .zero
    private var started = false
    private(set) var shake: Float = 0

    mutating func addShake(_ v: Float) { shake = min(1.2, shake + v) }

    mutating func snap(to car: CarSim, track: TrackData) {
        yaw = track.heading(track.wrap(car.proj.index + 14))
        focus = car.pos
        pos = idealPosition(car: car, yaw: yaw)
        started = true
    }

    private func idealPosition(car: CarSim, yaw: Float) -> SIMD3<Float> {
        let f = SIMD2(sin(yaw), cos(yaw))
        return SIMD3(car.pos.x - f.x * distance,
                     car.pos.y + height,
                     car.pos.z - f.y * distance)
    }

    /// Returns the camera transform. `blend` 0..1 eases out of the intro shot.
    mutating func update(car: CarSim, track: TrackData, dt: Float,
                         speedRatio: Float) -> (Transform, Float) {
        if !started { snap(to: car, track: track) }

        // where the camera points: mostly the track tangent a little ahead of
        // the car, with a touch of the car's actual heading so drifts read
        let ahead = track.wrap(car.proj.index + 16 + Int(car.speed * 0.5))
        let trackYaw = track.heading(ahead)
        var carYaw = car.heading
        if car.speed > 3 {
            carYaw = atan2(car.vel.x, car.vel.y)
        }
        let target = trackYaw + angleDelta(trackYaw, carYaw) * 0.22
        let k = 1 - exp(-3.4 * dt)
        yaw += angleDelta(yaw, target) * k

        // speed-dependent dynamics
        let d = distance * (1.0 + speedRatio * 0.22)
        let h = height * (1.0 + speedRatio * 0.10)
        let f = SIMD2(sin(yaw), cos(yaw))
        var want = SIMD3(car.pos.x - f.x * d, car.pos.y + h,
                         car.pos.z - f.y * d)
        // the camera must never dip below the track
        want.y = max(want.y, car.pos.y + height * 0.55)
        // ...nor into the hillside behind the car. On a steep descent the
        // road the car has just come down is higher than the car itself, so
        // the shot has to clear that, not only the car.
        let behind = track.pointAt(s: car.proj.s - d, lat: 0).y
        want.y = max(want.y, behind + height * 0.42)

        let pk = 1 - exp(-7.0 * dt)
        pos += (want - pos) * pk

        var lookAt = SIMD3(car.pos.x + f.x * lookAhead * (0.6 + speedRatio * 0.8),
                           car.pos.y + 1.0,
                           car.pos.z + f.y * lookAhead * (0.6 + speedRatio * 0.8))
        focus += (lookAt - focus) * (1 - exp(-8.0 * dt))
        lookAt = focus

        if shake > 0 {
            shake = max(0, shake - dt * 2.2)
            let a = Float.random(in: -1...1) * shake * 0.32
            let b = Float.random(in: -1...1) * shake * 0.32
            pos.x += a
            pos.y += b
        }

        var t = Transform()
        t.translation = pos
        t.rotation = lookRotation(from: pos, to: lookAt)
        let fov = fovDegrees + speedRatio * 7
        return (t, fov)
    }

    private func lookRotation(from: SIMD3<Float>,
                              to: SIMD3<Float>) -> simd_quatf {
        let forward = simd_normalize(to - from)
        let worldUp = SIMD3<Float>(0, 1, 0)
        var right = simd_cross(worldUp, -forward)
        if simd_length_squared(right) < 1e-6 { right = SIMD3(1, 0, 0) }
        right = simd_normalize(right)
        let up = simd_cross(-forward, right)
        let m = simd_float3x3(columns: (right, up, -forward))
        return simd_quatf(m)
    }
}
