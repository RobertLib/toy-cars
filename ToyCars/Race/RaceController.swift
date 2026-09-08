//
//  RaceController.swift
//  ToyCars
//
//  The glue between SwiftUI, the simulation and RealityKit. The loop is driven
//  by CADisplayLink so physics runs independently of SwiftUI redraws.
//

import Foundation
import Observation
import QuartzCore
import SwiftUI
import simd

@MainActor
@Observable
final class RaceController {
    var ready = false
    /// The track or the car could not be loaded. Without this the loading
    /// card stayed up for ever with no way off it - the pause button lives
    /// inside the `ready` branch, so a bad bundle was a dead end.
    var loadFailed = false
    var loadProgress: Double = 0
    /// Set through `setPaused` only, so the controls are always let go of
    /// with it - see `releaseControls`.
    private(set) var paused = false
    var engine: RaceEngine?
    var scene: RaceScene?

    /// Player input (set by ControlsOverlay).
    var steerInput: Float = 0
    var braking = false
    var drifting = false
    var autoThrottle = true
    /// Smoothed steering - the buttons give -1/0/1, but the car eases into
    /// lock gradually, otherwise steering would feel jerky.
    private var smoothSteer: Float = 0

    /// Average FPS over the last half second (diagnostics).
    var fps: Double = 0
    private var fpsFrames = 0
    private var fpsSince: CFTimeInterval = 0
    /// Debug mode: the AI drives the player's car (enabled by -autopilot).
    var autopilot = ProcessInfo.processInfo.arguments.contains("-autopilot")
    /// Steering debug test: holds a constant right-hand turn.
    private let steerTest = ProcessInfo.processInfo.arguments.contains("-steertest")
    private var steerTestLog: Double = 0
    private var autoDriver: AIDriver?

    var onRaceFinished: ((RaceEngine) -> Void)?
    private var finishHandled = false
    private var finishDelay: Double = 0

    private var link: CADisplayLink?
    private var lastTime: CFTimeInterval = 0
    private var trackData: TrackData?
    private var trackID: String = ""
    private var soundOn = true

    // ------------------------------------------------------------ setup

    func setup(trackID: String, carID: String, soundOn: Bool) async {
        guard !ready else { return }
        self.trackID = trackID
        self.soundOn = soundOn
        loadProgress = 0.05

        guard let data = TrackShapeCache.shared.data(trackID) else {
            loadFailed = true
            return
        }
        trackData = data
        loadProgress = 0.25

        let catalog = CarCatalog.shared
        guard let playerCar = catalog.car(carID) ?? catalog.cars.first else {
            loadFailed = true
            return
        }
        let difficulty = Float(data.file.difficulty - 1) / 2.0
        let opponents = pickOpponents(player: playerCar, count: 7,
                                      difficulty: difficulty)
        let e = RaceEngine(track: data, playerCar: playerCar,
                           opponents: opponents, difficulty: difficulty)
        engine = e
        loadProgress = 0.4

        let s = await RaceScene.build(track: data, engine: e)
        scene = s
        loadProgress = 0.9

        wireCallbacks(e)
        GameAudio.shared.enabled = soundOn
        GameAudio.shared.musicEnabled = GameProgress.shared.state.musicOn
        GameAudio.shared.start()
        GameAudio.shared.setMusicContext(0.19)
        Haptics.enabled = GameProgress.shared.state.hapticsEnabled
        Haptics.prepare()

        loadProgress = 1
        ready = true
    }

    func setPaused(_ on: Bool) {
        paused = on
        if on { releaseControls() }
    }

    /// Let go of everything the player was holding.
    ///
    /// A pedal is released by `DragGesture.onEnded`, and a gesture that is
    /// cancelled rather than ended - the pause panel coming up under a thumb,
    /// a system edge swipe, the app going to the background mid-corner -
    /// never sends one. The brake then stayed on for the rest of the race.
    /// Anything that takes the controls away from the player calls this.
    func releaseControls() {
        steerInput = 0
        smoothSteer = 0
        braking = false
        drifting = false
        engine?.setPlayerInput(DriverInput())
    }

    func restart(carID: String) async {
        stopLoop()
        releaseControls()
        // A restart is a fresh race and must not inherit the pause the panel
        // it was started from left behind. `tick` returns early while
        // `paused` is set, so the reloaded race used to sit frozen on the
        // grid with the controls dead - only opening the panel again and
        // pressing RESUME got out of it.
        setPaused(false)
        ready = false
        loadFailed = false
        engine = nil
        scene = nil
        finishHandled = false
        finishDelay = 0
        await setup(trackID: trackID, carID: carID, soundOn: soundOn)
        startLoop()
    }

    /// The field, matched to the player's car rather than drawn at random.
    ///
    /// Drawing freely from the whole catalogue put a 2000-coin Neo on the
    /// starting grid of the first, easiest circuit next to the free Bumble,
    /// and since car stats dominate these races the finishing order simply
    /// read back the price list. The AI's skill scaling cannot make up a
    /// difference in top speed. So the field is built around the player's own
    /// car instead: the models closest to it in class, widened by the track's
    /// difficulty so a later circuit still brings out the quick machinery.
    /// Internal rather than private so `OpponentFieldTests` can hold it to
    /// the promise above without having to build a whole race to do it.
    func pickOpponents(player: CarDef, count: Int,
                       difficulty: Float) -> [CarDef] {
        let all = CarCatalog.shared.cars.filter { $0.id != player.id }
        guard !all.isEmpty else { return [] }
        let ranked = all.sorted {
            abs($0.overall - player.overall) < abs($1.overall - player.overall)
        }
        // How many different models are eligible, not how wide a band in
        // rating: the grid is seven cars and the catalogue is eight, so a
        // rating band can never actually exclude anything - every race would
        // field the whole showroom no matter how the band was drawn. Four
        // models on the first circuit, the whole catalogue by the last.
        let distinct = min(ranked.count,
                           max(1, Int((4 + difficulty * 3).rounded())))
        let pool = Array(ranked.prefix(distinct))
        // Slots are filled from a bag that refills when empty, so the models
        // are spread evenly rather than one of them taking half the grid.
        var out: [CarDef] = []
        var bag = pool.shuffled()
        while out.count < count {
            if bag.isEmpty { bag = pool.shuffled() }
            out.append(bag.removeFirst())
        }
        return out
    }

    private func wireCallbacks(_ e: RaceEngine) {
        e.onPickup = { _ in
            GameAudio.shared.pickup()
            Haptics.tap()
        }
        e.onBoost = { _ in
            GameAudio.shared.boost()
            Haptics.bump(0.8)
        }
        e.onLap = {
            GameAudio.shared.lap()
            Haptics.success()
        }
        e.onCrash = { s in
            GameAudio.shared.crash(s)
            // Now that the engine sends the real closing speed, the worst
            // impacts get the heavy generator rather than a louder tap.
            if s > 0.85 { Haptics.crash() } else { Haptics.bump(CGFloat(s)) }
        }
        e.onCountdownBeep = { n in
            GameAudio.shared.beep(n)
            Haptics.bump(n == 0 ? 1.0 : 0.5)
        }
        e.onFinish = { [weak self] in
            GameAudio.shared.finish()
            Haptics.success()
            self?.finishDelay = 2.4
        }
    }

    // ------------------------------------------------------------ loop

    func startLoop() {
        guard link == nil else { return }
        let l = CADisplayLink(target: DisplayLinkProxy(self),
                              selector: #selector(DisplayLinkProxy.tick(_:)))
        l.preferredFrameRateRange = CAFrameRateRange(minimum: 30,
                                                     maximum: 120,
                                                     preferred: 60)
        l.add(to: .main, forMode: .common)
        link = l
        lastTime = CACurrentMediaTime()
    }

    func stopLoop() {
        link?.invalidate()
        link = nil
    }

    func teardown() {
        stopLoop()
        releaseControls()
        GameAudio.shared.silenceDriving()
        GameAudio.shared.setMusicContext(0.30)
    }

    fileprivate func tick(_ now: CFTimeInterval) {
        guard let e = engine, let s = scene else { return }
        var dt = now - lastTime
        lastTime = now
        if dt <= 0 || dt > 0.25 { dt = 1.0 / 60.0 }
        fpsFrames += 1
        if fpsSince == 0 { fpsSince = now }
        if now - fpsSince >= 0.5 {
            fps = Double(fpsFrames) / (now - fpsSince)
            fpsFrames = 0
            fpsSince = now
        }
        if paused {
            GameAudio.shared.silenceDriving()
            return
        }

        if autopilot, e.phase == .racing || e.phase == .finished {
            if autoDriver == nil { autoDriver = AIDriver(skill: 0.85, seed: 99) }
            autoDriver?.drive(car: e.playerCar, track: e.track,
                              others: e.cars, pickups: e.pickups,
                              rubber: 1.0, dt: Float(dt))
        } else {
            e.setPlayerInput(currentInput(e, dt: Float(dt)))
        }
        e.update(dt: dt)
        s.sync(engine: e, dt: Float(dt))

        let p = e.playerCar
        if e.phase == .racing || e.phase == .finished {
            GameAudio.shared.updateDriving(
                speed: p.speed, topSpeed: p.def.topSpeed,
                throttle: p.input.throttle, slip: p.slip,
                offTrack: p.offTrack, boost: p.boost > 0,
                airborne: p.airborne)
        } else {
            GameAudio.shared.updateDriving(
                speed: 0, topSpeed: p.def.topSpeed, throttle: 0.12,
                slip: 0, offTrack: false, boost: false, airborne: false)
        }

        if steerTest, e.phase == .racing {
            steerTestLog += dt
            if steerTestLog > 0.4 {
                steerTestLog = 0
                let p = e.playerCar
                print(String(format:
                    "STEERTEST steer=%+.2f lateral=%+.2f speed=%.1f heading=%+.2f",
                    p.input.steer, p.proj.lateral, p.speed, p.heading))
            }
        }

        if finishDelay > 0 {
            finishDelay -= dt
            if finishDelay <= 0 && !finishHandled {
                finishHandled = true
                // Torn down here rather than by whoever is listening. It
                // stops the display link, so the catch-up the handler runs
                // cannot be interleaved with live frames stepping the same
                // cars - and, just as importantly, it leaves the handler with
                // no reason to hold this controller. A closure that is stored
                // *on* the controller and captures it back is a cycle, and
                // the whole race hangs off it: the engine, the scene, eight
                // car entity trees and the effects pool.
                teardown()
                onRaceFinished?(e)
            }
        }
    }

    private func currentInput(_ e: RaceEngine, dt: Float) -> DriverInput {
        var i = DriverInput()
        // a gentle constant lock so the car traces an arc instead of spinning
        let target = steerTest ? 0.30 : max(-1, min(1, steerInput))
        // the car leans into a corner in ~0.15 s and recentres faster
        let rate: Float = abs(target) > abs(smoothSteer) ? 7.0 : 11.0
        smoothSteer += (target - smoothSteer) * min(1, dt * rate)
        if abs(smoothSteer) < 0.02 { smoothSteer = 0 }
        i.steer = smoothSteer
        if braking {
            i.throttle = -1
        } else if autoThrottle {
            i.throttle = 1
        }
        i.handbrake = drifting
        if e.playerCar.finished {
            i.throttle = 0.3
            i.steer *= 0.5
        }
        return i
    }
}

/// CADisplayLink retains its target strongly, hence this thin proxy.
private final class DisplayLinkProxy: NSObject {
    weak var owner: RaceController?
    init(_ o: RaceController) { owner = o }
    @objc func tick(_ link: CADisplayLink) {
        MainActor.assumeIsolated {
            owner?.tick(link.timestamp)
        }
    }
}
