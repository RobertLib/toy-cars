//
//  CarPreviewView.swift
//  ToyCars
//
//  The turntable the car models are shown on, in the garage and on the main
//  menu. It is a small photographic studio rather than a model viewer:
//
//  * a three point rig - key light from the front right with a real shadow,
//    a cool fill from the left so the shadow side is not black, and a rim
//    light behind tinted with the car's own paint, which is what separates the
//    body from the background;
//  * an equirectangular studio environment as the image based light, so the
//    paint picks up plausible reflections. It is not drawn as a dome: with no
//    environment set the RealityView is transparent, which lets the car stand
//    directly on the painted scene behind it instead of inside a box;
//  * a turntable the car actually stands on, catching the shadow. Without
//    something under the wheels a rotating model reads as floating.
//
//  The camera is deliberately low (eye level of a child looking at a toy on a
//  table) and long (21° field of view), which keeps the proportions of a small
//  car flattering instead of fish-eyed.
//

import RealityKit
import SwiftUI
import UIKit

struct CarPreviewView: View {
    let carID: String
    /// The car's paint, used for the rim light and the turntable trim.
    var accent: Color = TC.tangerine
    var spinSpeed: Float = 0.5
    var studio: Studio = .garage
    var showTurntable: Bool = true
    /// A RealityView on iOS composites over whatever is behind it as long as
    /// no environment is set, so the studio image is used for lighting only
    /// and the painted SwiftUI scene shows through around the car. Set this
    /// when the 3D dome should be the backdrop instead.
    var drawDome: Bool = false

    /// A model turning on its own for as long as the screen is open is the
    /// largest piece of standing motion in the whole interface, and it was
    /// the one piece that did not stop: the spin is driven by a `Timer` on
    /// `PreviewHolder`, which is not a `View` and so never saw the
    /// environment. Everything else here honours it - the clouds, the
    /// showroom flicker, the badge breathing right beside this car - so the
    /// turntable was the only thing left moving on a screen that had been
    /// asked to hold still. It now stops at the three-quarter pose the
    /// turntable starts from, which is the shot the stage is lit for.
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// Which room the car is standing in.
    enum Studio: Equatable {
        /// Dark showroom - the garage.
        case garage
        /// Warm outdoor light matching the menu backdrop.
        case sunset

        var zenith: UIColor {
            switch self {
            case .garage: return UIColor(hex: "#0e1524")
            case .sunset: return UIColor(hex: "#1b3d8f")
            }
        }
        var horizon: UIColor {
            switch self {
            case .garage: return UIColor(hex: "#2b3752")
            case .sunset: return UIColor(hex: "#ffd08a")
            }
        }
        var ground: UIColor {
            switch self {
            case .garage: return UIColor(hex: "#0a0e18")
            case .sunset: return UIColor(hex: "#2f6b4f")
            }
        }
        var sunColor: UIColor {
            switch self {
            case .garage: return UIColor(hex: "#ffffff")
            case .sunset: return UIColor(hex: "#ffd9a0")
            }
        }
        var keyIntensity: Float {
            switch self {
            case .garage: return 3600
            case .sunset: return 4200
            }
        }
        var iblExponent: Float {
            switch self {
            case .garage: return 0.35
            case .sunset: return 0.75
            }
        }
    }

    @State private var holder = PreviewHolder()

    var body: some View {
        RealityView { content in
            content.camera = .virtual
            let root = Entity()
            holder.root = root
            content.add(root)

            let pivot = Entity()
            root.addChild(pivot)
            holder.pivot = pivot
            // Struck here rather than left to the first tick of the spin
            // timer, because under Reduce Motion there is no first tick and
            // an unrotated pivot points the car straight down the lens - a
            // dead-on front view of a model whose key light comes from the
            // front right and whose stage is framed for three quarters.
            holder.applyPose()

            // ------------------------------------------------------ camera
            let cam = Entity()
            var pc = PerspectiveCameraComponent()
            pc.fieldOfViewInDegrees = 21
            pc.near = 0.05
            pc.far = 90
            cam.components.set(pc)
            cam.look(at: [0, 0.46, 0], from: [4.35, 2.90, 7.40],
                     relativeTo: nil)
            root.addChild(cam)

            // ------------------------------------------------------ lights
            // Key: front right, high, casts the shadow onto the turntable.
            let key = Entity()
            var kl = DirectionalLightComponent(color: studio.sunColor,
                                               intensity: studio.keyIntensity)
            kl.isRealWorldProxy = false
            key.components.set(kl)
            var shadow = DirectionalLightComponent.Shadow()
            shadow.depthBias = 1.2
            shadow.shadowProjection = .automatic(maximumDistance: 12)
            key.components.set(shadow)
            key.look(at: [0, 0.2, 0], from: [3.0, 4.2, 2.6], relativeTo: nil)
            root.addChild(key)

            // Fill: low from the left, cool, keeps the shadow side readable.
            let fill = Entity()
            var fl = DirectionalLightComponent(color: UIColor(hex: "#9fc4ff"),
                                               intensity: 900)
            fl.isRealWorldProxy = false
            fill.components.set(fl)
            fill.look(at: [0, 0.4, 0], from: [-4.0, 1.6, 3.0],
                      relativeTo: nil)
            root.addChild(fill)

            // Rim: behind, tinted with the paint. This is the light that makes
            // a dark car read against a dark room. Kept on the holder so a
            // change of car can retint it without rebuilding the studio.
            let rim = Entity()
            var rl = DirectionalLightComponent(color: UIColor(accent),
                                               intensity: 2200)
            rl.isRealWorldProxy = false
            rim.components.set(rl)
            rim.look(at: [0, 0.5, 0], from: [-2.0, 2.0, -5.0],
                     relativeTo: nil)
            root.addChild(rim)
            holder.rim = rim

            // ------------------------------------------------------ turntable
            if showTurntable {
                let stage = Self.makeTurntable(accent: accent, studio: studio)
                root.addChild(stage)
                holder.trim = stage.findEntity(named: "trim") as? ModelEntity
            }

            // ------------------------------------------------------ backdrop
            if let img = await SkyBuilder.makeImage(
                zenith: studio.zenith,
                horizon: studio.horizon,
                ground: studio.ground,
                sunDir: [-0.45, -0.72, -0.52],
                sunColor: studio.sunColor,
                clouds: studio == .sunset ? 0.35 : 0.0,
                size: CGSize(width: 512, height: 256)),
               let env = try? await EnvironmentResource(equirectangular: img) {
                if drawDome { content.environment = .skybox(env) }
                root.components.set(
                    ImageBasedLightComponent(
                        source: .single(env),
                        intensityExponent: studio.iblExponent))
                root.components.set(
                    ImageBasedLightReceiverComponent(imageBasedLight: root))
            }

            await holder.swap(to: carID)
        } update: { _ in
            // Swap the model and retint the two things that carry the car's
            // paint. Rebuilding the whole view instead (with `.id(carID)`)
            // would regenerate the environment image and the light rig for
            // every step through the garage.
            holder.request(carID)
            holder.retint(accent)
        }
        .realityViewCameraControls(.none)
        .onAppear { setSpin() }
        .onDisappear { holder.stopSpin() }
        // The setting can be changed while the app is open, so it is watched
        // rather than only read once.
        .onChange(of: reduceMotion) { _, _ in setSpin() }
    }

    private func setSpin() {
        if reduceMotion {
            holder.stopSpin()
            holder.applyPose()
        } else {
            holder.startSpin(speed: spinSpeed)
        }
    }

    // ------------------------------------------------------------ turntable

    /// A low disc with a bright trim ring in the car's paint colour. Two
    /// cylinders rather than one so the trim reads as a separate machined part
    /// and can glow without lighting the whole plinth.
    ///
    /// The radii are *worked out from the catalogue* rather than written
    /// down, because a written-down one goes stale the moment a longer car
    /// is added and nothing says so. It had: the deck was a flat 1.80, which
    /// is 3.6 m across, and Bolt is 4.35 m long - so the one car the garage
    /// was meant to show off best stood with its rear wing and back wheel
    /// hanging over the edge into the dark. The margin is generous enough to
    /// cover the bodywork a spec's `length` does not count, a formula car's
    /// wing above all.
    @MainActor
    private static func makeTurntable(accent: Color,
                                      studio: Studio) -> Entity {
        let stage = Entity()
        let longest = CarCatalog.shared.cars.map(\.length).max() ?? 4.4
        let deckR = longest * 0.5 + 0.22
        let ringR = deckR + 0.10
        let baseR = deckR + 0.22

        // Matte, not polished. A shiny deck mirrors the paint-tinted rim
        // light and the plinth turns the colour of the car - which reads as a
        // gold platter under a yellow car rather than as a stage.
        var deck = PhysicallyBasedMaterial()
        deck.baseColor = .init(tint: UIColor(hex: studio == .garage
                                             ? "#202a3e" : "#3a4a63"))
        deck.roughness = .init(floatLiteral: 0.72)
        deck.metallic = .init(floatLiteral: 0.05)
        let disc = ModelEntity(
            mesh: .generateCylinder(height: 0.09, radius: deckR),
            materials: [deck])
        disc.position = [0, -0.045, 0]
        stage.addChild(disc)

        // A thin lit line around the deck in the car's own paint - the neon
        // rim a show plinth has. Unlit, so it holds its colour whatever the
        // studio lighting does, but kept to a few millimetres: a wide unlit
        // band on a yellow car turns the whole plinth into the brightest
        // thing on screen.
        var trim = UnlitMaterial()
        trim.color = .init(tint: UIColor(accent.lighter(0.10)))
        let ring = ModelEntity(
            mesh: .generateCylinder(height: 0.014, radius: ringR),
            materials: [trim])
        ring.name = "trim"
        ring.position = [0, -0.030, 0]
        stage.addChild(ring)

        var plinth = PhysicallyBasedMaterial()
        plinth.baseColor = .init(tint: UIColor(hex: "#0b1020"))
        plinth.roughness = .init(floatLiteral: 0.7)
        plinth.metallic = .init(floatLiteral: 0.1)
        let base = ModelEntity(
            mesh: .generateCylinder(height: 0.22, radius: baseR),
            materials: [plinth])
        base.position = [0, -0.18, 0]
        stage.addChild(base)

        return stage
    }
}

@MainActor
@Observable
final class PreviewHolder {
    var root: Entity?
    var pivot: Entity?
    /// The rim light and the turntable trim - the two parts that wear the
    /// car's paint and so have to change with it.
    var rim: Entity?
    var trim: ModelEntity?
    /// The car the scene currently shows, and the car most recently asked
    /// for. They differ while a model is loading, which is what lets a stale
    /// load be thrown away instead of added on top of the new car.
    private var currentID: String = ""
    private var requestedID: String = ""
    private var loadTask: Task<Void, Never>?
    private var accent: Color?
    private var model: Entity?
    private var timer: Timer?
    private var angle: Float = 0.9

    func retint(_ color: Color) {
        guard accent != color else { return }
        accent = color
        if var light = rim?.components[DirectionalLightComponent.self] {
            light.color = UIColor(color)
            rim?.components.set(light)
        }
        if let trim {
            var m = UnlitMaterial()
            m.color = .init(tint: UIColor(color.lighter(0.10)))
            trim.model?.materials = [m]
        }
    }

    /// Loading a model is asynchronous, so stepping quickly through the
    /// garage can have several loads in flight at once. Left unmanaged they
    /// finish in whatever order they please and each adds its car to the
    /// pivot, so the models pile up on the turntable. One load at a time,
    /// and only the newest one gets to be shown.
    func request(_ id: String) {
        guard id != requestedID else { return }
        requestedID = id
        let previous = loadTask
        previous?.cancel()
        loadTask = Task { [weak self] in
            await previous?.value
            await self?.swap(to: id)
        }
    }

    /// Called directly once while the studio is being built, and through
    /// `request` for every change of car after that.
    func swap(to id: String) async {
        if requestedID.isEmpty { requestedID = id }
        currentID = id
        guard let m = try? await Entity(named: "car_\(id)", in: nil),
              !Task.isCancelled, currentID == id else {
            // A newer car was asked for while this one was loading - drop it.
            return
        }
        // Clear the turntable rather than only the model we remember: if an
        // earlier load ever slipped through, this is what takes it back off.
        if let pivot {
            for child in pivot.children.map({ $0 }) { child.removeFromParent() }
        }
        m.position = [0, 0, 0]
        pivot?.addChild(m)
        model = m
    }

    func startSpin(speed: Float) {
        stopSpin()
        // The timer runs on the main loop, so we are already on the MainActor.
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0,
                                     repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick(speed: speed) }
        }
    }

    private func tick(speed: Float) {
        angle += speed / 60
        applyPose()
    }

    /// Puts the turntable where `angle` says, without a timer. Called once as
    /// the studio is built so the car has its pose before the first tick -
    /// and it is the whole of the pose under Reduce Motion, where there is no
    /// tick at all.
    func applyPose() {
        pivot?.orientation = simd_quatf(angle: angle, axis: [0, 1, 0])
    }

    func stopSpin() {
        timer?.invalidate()
        timer = nil
    }
}
