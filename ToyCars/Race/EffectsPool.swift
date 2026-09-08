//
//  EffectsPool.swift
//  ToyCars
//
//  Smoke puffs and skid marks. Everything is preallocated and recycled -
//  nothing is created or freed while driving.
//

import Foundation
import RealityKit
import UIKit
import simd

@MainActor
final class EffectsPool {
    let root = Entity()

    private struct Puff {
        var entity: ModelEntity
        var vel: SIMD3<Float> = .zero
        var life: Float = 0
        var maxLife: Float = 1
        var size0: Float = 0.3
        var size1: Float = 1.0
        var active = false
    }

    private struct Mark {
        var entity: ModelEntity
        var life: Float = 0
        var active = false
    }

    private var puffs: [Puff] = []
    private var marks: [Mark] = []
    private var puffCursor = 0
    private var markCursor = 0

    // The two numbers that decide how long a skid trail can be, and they have
    // to be read together. Marks were laid two per *frame*, so a car put down
    // 120 a second and the 140-mark ring wrapped in 1.2 s - against a 4.5 s
    // life with a fade that does not even start until 55 % of it. Every mark
    // was therefore recycled while fully opaque: the trail popped out of
    // existence instead of fading, it got shorter the faster you went, and
    // with four cars sliding it was down to a third of a second.
    //
    // Laying them by distance instead makes the trail a property of the road
    // rather than of the frame rate, and these three now agree: 2 marks every
    // 1.6 m is 31 a second at 25 m/s, so one car fills the ring in 6.4 s and
    // two in 3.2 - both clear of the life below, which is what lets a mark
    // reach its fade.
    private let markLife: Float = 2.6
    private let markSpacing: Float = 1.6

    private var puffMaterials: [Int: UnlitMaterial] = [:]
    private var boostMaterial = UnlitMaterial()
    private var emitAccum: [Float] = []
    private var markAccum: [Float] = []

    init(puffCount: Int = 72, markCount: Int = 200) {
        for s in [Surface.asphalt, .dirt, .sand, .snow, .ice, .wood] {
            let c = s.dustColor
            puffMaterials[s.rawValue] = EffectsPool.unlit(
                UIColor(red: CGFloat(c.x), green: CGFloat(c.y),
                        blue: CGFloat(c.z), alpha: 1), opacity: 1)
        }
        boostMaterial = EffectsPool.unlit(
            UIColor(red: 0.34, green: 0.92, blue: 1.0, alpha: 1), opacity: 1)

        let sphere = MeshResource.generateSphere(radius: 0.5)
        let fallback = puffMaterials[Surface.asphalt.rawValue]!
        for _ in 0..<puffCount {
            let e = ModelEntity(mesh: sphere, materials: [fallback])
            e.isEnabled = false
            e.components.set(OpacityComponent(opacity: 0))
            root.addChild(e)
            puffs.append(Puff(entity: e))
        }

        let plane = MeshResource.generatePlane(width: 1, depth: 1)
        let markMat = EffectsPool.unlit(UIColor(white: 0.07, alpha: 1),
                                        opacity: 0.45)
        for _ in 0..<markCount {
            let e = ModelEntity(mesh: plane, materials: [markMat])
            e.isEnabled = false
            e.components.set(OpacityComponent(opacity: 0))
            root.addChild(e)
            marks.append(Mark(entity: e))
        }
    }

    private static func unlit(_ color: UIColor, opacity: Float) -> UnlitMaterial {
        var m = UnlitMaterial()
        m.color = .init(tint: color)
        m.blending = .transparent(opacity: .init(floatLiteral: opacity))
        return m
    }

    // ------------------------------------------------------------ spawn

    private func spawnPuff(at p: SIMD3<Float>, vel: SIMD3<Float>,
                           surface: Surface, size: Float, life: Float,
                           material: UnlitMaterial? = nil) {
        guard !puffs.isEmpty else { return }
        var idx = puffCursor
        for _ in 0..<puffs.count {
            if !puffs[idx].active { break }
            idx = (idx + 1) % puffs.count
        }
        puffCursor = (idx + 1) % puffs.count

        let e = puffs[idx].entity
        e.model?.materials = [material
                              ?? puffMaterials[surface.rawValue]
                              ?? puffMaterials[Surface.asphalt.rawValue]!]
        e.position = p
        e.scale = SIMD3(repeating: size)
        e.isEnabled = true
        e.components.set(OpacityComponent(opacity: 0.8))
        puffs[idx].vel = vel
        puffs[idx].life = 0
        puffs[idx].maxLife = life
        puffs[idx].size0 = size
        puffs[idx].size1 = size * 2.5
        puffs[idx].active = true
    }

    private func spawnMark(at p: SIMD3<Float>, heading: Float, width: Float,
                           length: Float) {
        guard !marks.isEmpty else { return }
        let idx = markCursor
        markCursor = (markCursor + 1) % marks.count
        let e = marks[idx].entity
        e.position = SIMD3(p.x, p.y + 0.04, p.z)
        // the plane lies in XZ and its local +Z is the length axis - the same
        // convention as forward = (sin h, cos h), so we rotate straight by heading
        e.orientation = simd_quatf(angle: heading, axis: SIMD3<Float>(0, 1, 0))
        e.scale = SIMD3(width, 1, length)
        e.isEnabled = true
        e.components.set(OpacityComponent(opacity: 0.45))
        marks[idx].life = 0
        marks[idx].active = true
    }

    // ------------------------------------------------------------ update

    func update(dt: Float, engine: RaceEngine, cameraPos: SIMD3<Float>) {
        emit(dt: dt, engine: engine, cameraPos: cameraPos)

        for i in puffs.indices where puffs[i].active {
            puffs[i].life += dt
            let u = puffs[i].life / puffs[i].maxLife
            if u >= 1 {
                puffs[i].active = false
                puffs[i].entity.isEnabled = false
                continue
            }
            let e = puffs[i].entity
            e.position += puffs[i].vel * dt
            puffs[i].vel *= (1 - min(0.9, 1.8 * dt))
            puffs[i].vel.y += 1.3 * dt
            let s = puffs[i].size0 + (puffs[i].size1 - puffs[i].size0) * u
            e.scale = SIMD3(repeating: s)
            e.components.set(OpacityComponent(opacity: 0.8 * (1 - u * u)))
        }

        for i in marks.indices where marks[i].active {
            marks[i].life += dt
            let u = marks[i].life / markLife
            if u >= 1 {
                marks[i].active = false
                marks[i].entity.isEnabled = false
            } else if u > 0.55 {
                let f = 1 - (u - 0.55) / 0.45
                marks[i].entity.components.set(
                    OpacityComponent(opacity: 0.45 * f))
            }
        }
    }

    private func emit(dt: Float, engine: RaceEngine, cameraPos: SIMD3<Float>) {
        if emitAccum.count != engine.cars.count {
            emitAccum = Array(repeating: 0, count: engine.cars.count)
            markAccum = Array(repeating: 0, count: engine.cars.count)
        }
        guard engine.phase == .racing || engine.phase == .finished else { return }

        for (i, car) in engine.cars.enumerated() {
            let far = simd_distance(car.pos, cameraPos)
            if far > 75 { continue }
            let detail: Float = far < 32 ? 1 : 0.35

            let sliding = car.slip > 0.26 && car.speed > 6
            let dusty = car.offTrack && car.speed > 4
            let boosting = car.boost > 0

            // Skid marks are laid down the road, not down the clock - see
            // `markSpacing`. A car that is not sliding forgets what it had
            // banked, so a fresh slide starts with a mark rather than
            // wherever the last one happened to leave off.
            if sliding && !car.offTrack && !car.airborne && far < 48 {
                markAccum[i] += car.speed * dt
                if markAccum[i] >= markSpacing {
                    let run = min(markAccum[i], markSpacing * 3)
                    markAccum[i] = 0
                    let rear = -car.forward
                    for s in [Float(-1), 1] {
                        let off = car.right * (car.def.width * 0.40 * s)
                        let p = SIMD3(
                            car.pos.x + rear.x * car.def.wheelbase * 0.5 + off.x,
                            car.pos.y,
                            car.pos.z + rear.y * car.def.wheelbase * 0.5 + off.y)
                        spawnMark(at: p, heading: car.heading, width: 0.28,
                                  length: run * 1.15)
                    }
                }
            } else {
                markAccum[i] = markSpacing
            }

            var rate: Float = 0
            if sliding { rate += 24 * car.slip }
            if dusty { rate += 20 }
            if boosting { rate += 28 }
            if car.landedHard > 0.25 { rate += 55 }
            rate *= detail
            guard rate > 0 else { continue }

            emitAccum[i] += rate * dt
            let n = min(4, Int(emitAccum[i]))
            emitAccum[i] -= Float(n)
            guard n > 0 else { continue }

            let back = -car.forward
            let side = car.right
            for _ in 0..<n {
                let lat = Float.random(in: -1...1) * car.def.width * 0.42
                let p = SIMD3(car.pos.x + back.x * car.def.length * 0.42
                              + side.x * lat,
                              car.pos.y + 0.20,
                              car.pos.z + back.y * car.def.length * 0.42
                              + side.y * lat)
                let jitter = SIMD3<Float>(Float.random(in: -0.7...0.7),
                                          Float.random(in: 0.3...1.3),
                                          Float.random(in: -0.7...0.7))
                if boosting {
                    spawnPuff(at: p,
                              vel: jitter * 0.6 + SIMD3(back.x, 0, back.y) * 4,
                              surface: car.surface, size: 0.26, life: 0.32,
                              material: boostMaterial)
                } else {
                    spawnPuff(at: p,
                              vel: jitter + SIMD3(back.x, 0, back.y) * 1.5,
                              surface: car.surface,
                              size: Float.random(in: 0.20...0.40),
                              life: Float.random(in: 0.5...0.9))
                }
            }

        }
    }
}
