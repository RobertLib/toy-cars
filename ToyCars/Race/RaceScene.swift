//
//  RaceScene.swift
//  ToyCars
//
//  Building the RealityKit scene and keeping it in sync with the simulation.
//

import Foundation
import RealityKit
import UIKit
import simd

/// A single car in the scene.
@MainActor
final class CarNode {
    let root = Entity()
    let tilt = Entity()
    var model: Entity?
    var wheels: [Entity?] = [nil, nil, nil, nil]   // FL, FR, RL, RR
    var boostGlow: Entity?

    init(model: Entity?) {
        root.addChild(tilt)
        if let m = model {
            tilt.addChild(m)
            self.model = m
            let names = ["WheelFL", "WheelFR", "WheelRL", "WheelRR"]
            for (i, n) in names.enumerated() {
                wheels[i] = m.findEntity(named: n)
            }
        }
    }

    func apply(_ car: CarSim) {
        root.position = car.pos
        // Blender exports with a -90 root rotation around X, so in RealityKit
        // the model faces +Z (not -Z as hand-made USDZ files do).
        root.orientation = simd_quatf(angle: car.heading,
                                      axis: SIMD3<Float>(0, 1, 0))
        tilt.orientation = simd_quatf(angle: car.bodyRoll,
                                      axis: SIMD3<Float>(0, 0, 1))
            * simd_quatf(angle: car.bodyPitch + car.groundPitch,
                         axis: SIMD3<Float>(1, 0, 0))
        // The wheels sit inside that root rotation, so their local axes are
        // still in Blender's frame: axle = X, steering = Z.
        let spin = simd_quatf(angle: car.wheelSpin, axis: SIMD3<Float>(1, 0, 0))
        // After the root rotation the steering axis (local Z) points up along
        // +Y, so a positive angle would turn the wheels left - but positive
        // input means right, hence the sign flip.
        let steer = simd_quatf(angle: -car.steerAngle,
                               axis: SIMD3<Float>(0, 0, 1))
        wheels[0]?.orientation = steer * spin
        wheels[1]?.orientation = steer * spin
        wheels[2]?.orientation = spin
        wheels[3]?.orientation = spin
        boostGlow?.isEnabled = car.boost > 0
    }
}

@MainActor
final class RaceScene {
    let root = Entity()
    private(set) var carNodes: [CarNode] = []
    private var canNodes: [Entity] = []
    let cameraEntity = Entity()
    private var camera = ChaseCamera()
    private var sun = Entity()
    private var effects: EffectsPool!
    private var trackEntity: Entity?
    private(set) var environment: EnvironmentResource?
    private var introTime: Float = 0
    private var playerMarker: Entity?
    private var markerPhase: Float = 0

    let track: TrackData
    let theme: TrackFile

    private init(track: TrackData) {
        self.track = track
        self.theme = track.file
    }

    // ------------------------------------------------------------ loading

    static func build(track: TrackData, engine: RaceEngine) async -> RaceScene {
        let scene = RaceScene(track: track)
        await scene.load(engine: engine)
        return scene
    }

    private func load(engine: RaceEngine) async {
        // --- track
        if let t = try? await Entity(named: "track_\(track.file.id)",
                                     in: nil) {
            trackEntity = t
            root.addChild(t)
        }

        // --- lights
        var light = DirectionalLightComponent(
            color: UIColor(hex: theme.sunColor),
            intensity: theme.sunIntensity * 2600)
        light.isRealWorldProxy = false
        sun.components.set(light)
        var shadow = DirectionalLightComponent.Shadow()
        shadow.depthBias = 1.6
        shadow.shadowProjection = .automatic(maximumDistance: 58)
        sun.components.set(shadow)
        let d = simd_normalize(SIMD3<Float>(theme.sun[0], theme.sun[1],
                                            theme.sun[2]))
        sun.look(at: d, from: .zero, relativeTo: nil)
        root.addChild(sun)

        // --- sky + IBL
        if let img = await SkyBuilder.makeImage(
            zenith: UIColor(hex: theme.sky2),
            horizon: UIColor(hex: theme.sky),
            ground: UIColor(hex: theme.fog),
            sunDir: d,
            sunColor: UIColor(hex: theme.sunColor),
            clouds: theme.theme == "winter" ? 0.35 : 0.55),
           let env = try? await EnvironmentResource(equirectangular: img) {
            environment = env
        }

        // --- cars
        var cache: [String: Entity] = [:]
        for car in engine.cars {
            var model: Entity?
            if let c = cache[car.def.id] {
                model = c.clone(recursive: true)
            } else if let m = try? await Entity(named: "car_\(car.def.id)",
                                               in: nil) {
                cache[car.def.id] = m
                model = m.clone(recursive: true)
            }
            let node = CarNode(model: model)
            node.apply(car)
            carNodes.append(node)
            root.addChild(node.root)
        }

        // --- fuel canisters
        if let can = try? await Entity(named: "pickup_canister", in: nil) {
            for p in engine.pickups {
                let e = Entity()
                e.addChild(can.clone(recursive: true))
                e.position = p.pos
                canNodes.append(e)
                root.addChild(e)
            }
        }

        // --- player marker
        if let pi = engine.cars.firstIndex(where: { $0.isPlayer }),
           pi < carNodes.count {
            let marker = RaceScene.makeMarker()
            marker.position = [0, 2.6, 0]
            carNodes[pi].root.addChild(marker)
            playerMarker = marker
        }

        // --- camera
        var cam = PerspectiveCameraComponent()
        cam.fieldOfViewInDegrees = camera.fovDegrees
        cam.near = 0.4
        cam.far = 900
        cameraEntity.components.set(cam)
        root.addChild(cameraEntity)
        camera.snap(to: engine.player, track: track)

        // --- effects
        effects = EffectsPool()
        root.addChild(effects.root)
    }

    // ------------------------------------------------------------ update

    func sync(engine: RaceEngine, dt: Float) {
        for (i, car) in engine.cars.enumerated() where i < carNodes.count {
            carNodes[i].apply(car)
        }
        for (i, p) in engine.pickups.enumerated() where i < canNodes.count {
            let e = canNodes[i]
            e.isEnabled = p.active
            if p.active {
                e.orientation = simd_quatf(angle: p.spin,
                                           axis: SIMD3<Float>(0, 1, 0))
                e.position.y = p.pos.y + sin(p.bob) * 0.16
            }
        }

        let p = engine.player
        let ratio = min(1, p.speed / max(6, p.def.topSpeed))
        // intro shot: the camera starts high above the grid and glides down
        // into the racing position once the countdown begins
        if engine.phase == .intro {
            introTime += dt
            camera.height = 34
            camera.distance = 6
            camera.fovDegrees = 44
        } else if introTime > 0 {
            let k = min(1, dt * 1.4)
            camera.height += (16.5 - camera.height) * k
            camera.distance += (12.0 - camera.distance) * k
            camera.fovDegrees += (50 - camera.fovDegrees) * k
        }
        let (t, fov) = camera.update(car: p, track: track, dt: dt,
                                     speedRatio: ratio)
        cameraEntity.transform = t
        if var cam = cameraEntity.components[PerspectiveCameraComponent.self] {
            cam.fieldOfViewInDegrees = fov
            cameraEntity.components.set(cam)
        }
        if p.bumpFlash > 0.9 { camera.addShake(0.55) }
        if p.landedHard > 0.6 { camera.addShake(0.35) }

        markerPhase += dt * 3.4
        if let m = playerMarker {
            m.position.y = 2.5 + sin(markerPhase) * 0.20
            m.orientation = simd_quatf(angle: markerPhase * 0.4,
                                       axis: SIMD3<Float>(0, 1, 0))
        }

        effects.update(dt: dt, engine: engine, cameraPos: t.translation)
    }

    func shake(_ v: Float) { camera.addShake(v) }

    /// Arrow above the player's car - otherwise it gets lost in a field of eight.
    private static func makeMarker() -> Entity {
        // RealityView itself needs iOS 18, so the sphere fallback this used to
        // carry could never be reached.
        let mesh = MeshResource.generateCone(height: 1.25, radius: 0.62)
        var mat = UnlitMaterial()
        mat.color = .init(tint: UIColor(hex: "#ff3d6e"))
        let e = ModelEntity(mesh: mesh, materials: [mat])
        e.orientation = simd_quatf(angle: .pi, axis: SIMD3<Float>(0, 0, 1))
        var ringMat = UnlitMaterial()
        ringMat.color = .init(tint: UIColor(hex: "#ffc21c"))
        ringMat.blending = .transparent(opacity: .init(floatLiteral: 0.55))
        let ring = ModelEntity(
            mesh: MeshResource.generatePlane(width: 3.4, depth: 3.4,
                                             cornerRadius: 1.7),
            materials: [ringMat])
        ring.position = [0, -2.55, 0]
        let wrap = Entity()
        wrap.addChild(e)
        wrap.addChild(ring)
        return wrap
    }
}
