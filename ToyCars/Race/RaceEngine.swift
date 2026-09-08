//
//  RaceEngine.swift
//  ToyCars
//
//  Race orchestration: countdown, laps, standings, canisters, boost, finish.
//  Runs on a fixed timestep so physics behaves identically on every device.
//

import Foundation
import Observation
import simd

struct PickupState {
    var s: Float
    var lat: Float
    var pos: SIMD3<Float>
    var active: Bool = true
    var respawn: Float = 0
    var spin: Float = 0
    var bob: Float = 0
}

struct BoostPadState {
    var s: Float
    var lat: Float
    var pos: SIMD3<Float>
}

enum RacePhase: Equatable {
    case intro          // camera fly-through before the start
    case countdown
    case racing
    case finished
}

@MainActor
@Observable
final class RaceEngine {
    // ------------------------------------------------------------ public
    var phase: RacePhase = .intro
    var countdown: Double = 3.4
    var raceTime: Double = 0
    var playerLap: Int = 1
    var playerPosition: Int = 1
    var playerSpeed: Float = 0
    var playerFuel: Float = 1
    var playerBoost: Float = 0
    var playerBestLap: Double = .infinity
    var lastLapTime: Double = 0
    var lapFlash: Double = 0
    var messages: [RaceMessage] = []
    var finalOrder: [CarSim] = []
    /// Standings frozen at the moment the player crossed the finish line.
    var finalStandings: [Standing] = []

    struct Standing: Sendable {
        var name: String
        var color: String
        var time: Double
        var finished: Bool
        var isPlayer: Bool
    }
    var totalLaps: Int = 3
    var playerOutOfFuel = false
    var playerFinalPosition = 0
    var playerOffTrack = false
    var wrongWay = false

    /// The fraction of a tank at which the game starts warning, shared by all
    /// three things that do the warning: this class's on-screen message, the
    /// red wash over the whole screen in `HUDView`, and the pulse on
    /// `FuelGauge` itself. The gauge used to carry its own 0.20 against the
    /// other two on 0.18, so between those figures the instrument flashed on
    /// its own with nothing else agreeing - which reads as an instrument
    /// fault rather than as a low tank.
    static let lowFuelFraction: Float = 0.18
    /// Where the warning is armed again, high enough above the threshold that
    /// a canister taken on the limit cannot chatter it.
    static let refuelledFraction: Float = 0.30

    struct RaceMessage: Identifiable {
        let id = UUID()
        var text: String
        var kind: Kind
        var born: Double
        enum Kind { case good, warn, info }
    }

    // ------------------------------------------------------------ internal
    let track: TrackData
    private(set) var cars: [CarSim] = []
    private(set) var pickups: [PickupState] = []
    private(set) var boostPads: [BoostPadState] = []
    var player: CarSim { cars[playerIndex] }
    private var playerIndex = 0
    /// What each car is called in the standings, indexed by grid slot.
    ///
    /// Not simply `def.name`. The field is drawn from the models nearest the
    /// player's own in class, and on the first circuit that pool is four
    /// models for seven slots - so the grid always carries repeats, and the
    /// results table came out reading "Scoop, Scoop, Chili, Chili, Bumble,
    /// Bumble" with the same paint swatch beside each pair and nothing to
    /// tell one from the other. The repeats are the right call (a pool wide
    /// enough to avoid them is the whole showroom, Neo included, on the
    /// easiest race), so the fix belongs here: the second Chili on the grid
    /// is Chili II, which is how a real one-make entry list reads. Numbered
    /// by slot, so a car's name never changes during a race.
    private(set) var displayNames: [String] = []
    private var accumulator: Double = 0
    private let fixedDT: Float = 1.0 / 120.0
    /// The step `fastForwardToFinish` catches the opponents up on - see there.
    private let catchUpDT: Float = 1.0 / 60.0
    private var introTimer: Double = 0
    let fieldSize: Int

    /// Outbound callbacks - for sound and effects.
    var onPickup: ((SIMD3<Float>) -> Void)?
    var onBoost: ((SIMD3<Float>) -> Void)?
    var onLap: (() -> Void)?
    var onCrash: ((Float) -> Void)?
    var onCountdownBeep: ((Int) -> Void)?
    var onFinish: (() -> Void)?
    private var lastBeep = -1
    /// The clock the on-screen messages age on. `raceTime` will not do: it
    /// only advances once the lights go out, so a message pushed during the
    /// countdown would sit there for the rest of the race.
    private var messageClock: Double = 0
    private var stalledTimer: Float = 0
    private var allOthersDoneTimer: Float = 0
    private var dnfWarned = false

    init(track: TrackData, playerCar: CarDef, opponents: [CarDef],
         difficulty: Float) {
        self.track = track
        var laps = track.file.laps
        let args = ProcessInfo.processInfo.arguments
        if let i = args.firstIndex(of: "-laps"), i + 1 < args.count,
           let n = Int(args[i + 1]) { laps = max(1, n) }
        self.totalLaps = laps
        self.fieldSize = opponents.count + 1
        let grid = track.file.grid
        var defs: [CarDef] = [playerCar] + opponents
        // the player starts mid-field, so there is someone to chase as well
        let playerSlot = min(grid.count - 1, max(0, defs.count / 2))
        defs.remove(at: 0)
        defs.insert(playerCar, at: playerSlot)
        playerIndex = playerSlot

        for (i, def) in defs.enumerated() {
            let g = grid[min(i, grid.count - 1)]
            let p = SIMD3<Float>(g.x, g.y, g.z)
            let proj = track.projectGlobal(p)
            let sim = CarSim(def: def, slot: i, isPlayer: i == playerSlot,
                             track: track, start: p,
                             heading: track.heading(proj.index))
            if i != playerSlot {
                let skill = min(0.98, 0.42 + difficulty * 0.30
                                + Float(i % 4) * 0.055)
                sim.ai = AIDriver(skill: skill, seed: i * 31 + 7)
            }
            if i == playerSlot,
               ProcessInfo.processInfo.arguments.contains("-nofuel") {
                sim.fuel = 5      // debug test of running out of fuel
            }
            cars.append(sim)
        }
        displayNames = RaceEngine.nameField(defs)

        for c in track.file.cans {
            // The lift is measured from whatever the car drives on, and over
            // a jump that is the ramp rather than the road buried under it.
            // Taking it off the centreline alone put Palm Cove's canister at
            // s=262 - which sits on a 2.2 m ramp - a metre *inside* the ramp
            // body, and the ramp spans 98 % of the road, so the crate was
            // simply invisible. It still collected, because the test is a
            // distance in the ground plane: forty litres out of thin air,
            // with the AI driving over to fetch something that is not there.
            let lift = 0.55 + track.rampHeight(atS: c.s).height
            let p = track.pointAt(s: c.s, lat: c.lat, lift: lift)
            pickups.append(PickupState(s: c.s, lat: c.lat, pos: p))
        }
        for b in track.file.boosts {
            let p = track.pointAt(s: b.s, lat: b.lat, lift: 0.1)
            boostPads.append(BoostPadState(s: b.s, lat: b.lat, pos: p))
        }
        introTimer = 2.6
    }

    /// One name per grid slot, with any model that appears more than once
    /// numbered in the order it sits on the grid: `Chili`, `Chili II`,
    /// `Chili III`. A model that appears once is left alone - most of the
    /// field is unique and a bare name is what a player expects to read.
    ///
    /// Roman numerals rather than "#2" or "(2)": they need no translating,
    /// they cannot be mistaken for a lap or a placing, and they are what a
    /// team's second entry is actually called.
    static func nameField(_ defs: [CarDef]) -> [String] {
        var total: [String: Int] = [:]
        for d in defs { total[d.id, default: 0] += 1 }
        var seen: [String: Int] = [:]
        return defs.map { d in
            guard total[d.id, default: 0] > 1 else { return d.name }
            seen[d.id, default: 0] += 1
            return "\(d.name) \(RaceEngine.roman(seen[d.id] ?? 1))"
        }
    }

    /// Roman numerals for the range a grid can actually produce - a field of
    /// eight cannot repeat one model more than eight times.
    private static func roman(_ n: Int) -> String {
        let numerals = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII"]
        return numerals.indices.contains(n - 1) ? numerals[n - 1] : "\(n)"
    }

    // ------------------------------------------------------------ loop

    func update(dt raw: Double) {
        let dt = min(raw, 1.0 / 20.0)
        switch phase {
        case .intro:
            introTimer -= dt
            if introTimer <= 0 { phase = .countdown }
        case .countdown:
            countdown -= dt
            let n = Int(ceil(countdown))
            if n != lastBeep && n >= 0 && n <= 3 {
                lastBeep = n
                onCountdownBeep?(n)
            }
            if countdown <= 0 { phase = .racing }
        case .racing, .finished:
            raceTime += dt
        }

        accumulator += dt
        var steps = 0
        while accumulator >= Double(fixedDT) && steps < 6 {
            simulate(fixedDT)
            accumulator -= Double(fixedDT)
            steps += 1
        }
        if steps == 6 { accumulator = 0 }

        updateVisualsState(Float(dt))
        messageClock += dt
        messages.removeAll { messageClock - $0.born > 2.6 }
    }

    private func simulate(_ dt: Float) {
        let racing = (phase == .racing || phase == .finished)

        // --- AI
        if racing {
            for c in cars where c.ai != nil && !c.finished {
                let rubber = rubberBand(for: c)
                c.ai?.drive(car: c, track: track, others: cars,
                            pickups: pickups, rubber: rubber, dt: dt)
            }
            for c in cars where c.finished {
                c.input = DriverInput(throttle: 0.35, steer: c.input.steer,
                                      handbrake: false)
            }
        }

        // --- physics
        for c in cars {
            c.step(dt: dt, track: track, racing: racing)
        }

        // --- car collisions
        if cars.count > 1 {
            for i in 0..<(cars.count - 1) {
                for j in (i + 1)..<cars.count {
                    let impact = cars[i].bump(with: cars[j], dt: dt)
                    guard impact > 0,
                          cars[i].isPlayer || cars[j].isPlayer else { continue }
                    // The closing speed, not a constant. `GameAudio.crash`
                    // grades its buffer by this and so does the haptic, and
                    // both of them used to be handed 0.6 every single time -
                    // a nudge in traffic sounded exactly like a head-on.
                    // `CarSim.hardBump` is the floor; 22 m/s above it is
                    // more than this field can produce.
                    onCrash?(min(1, 0.45 + (impact - CarSim.hardBump) / 22))
                }
            }
        }

        if racing { updateRace(dt) }
    }

    // ------------------------------------------------------------ rules

    /// Whether any car still in the race is standing on a spot.
    private func isOccupied(_ p: SIMD3<Float>, radius: Float) -> Bool {
        cars.contains { c in
            guard !c.finished else { return false }
            let d = SIMD2(c.pos.x - p.x, c.pos.z - p.z)
            return simd_length_squared(d) < radius * radius
        }
    }

    private func updateRace(_ dt: Float) {
        // --- fuel canisters
        //
        // A canister has to be *driven to*, so one comes back only once the
        // ground it stands on is clear. Re-arming on the timer alone paid a
        // car that was simply parked on it: the crate reappeared underneath
        // and was collected again on the very next step - 40 litres every
        // seven seconds, or close to six a second, against the 1.6 a second
        // the thirstiest car burns flat out. A car wedged on one could not be
        // made to run dry at all. Driving through at racing speed clears the
        // radius in about a tenth of a second, two orders of magnitude inside
        // the respawn, so collecting them normally is untouched.
        let radius: Float = 2.6
        for i in pickups.indices {
            if !pickups[i].active {
                pickups[i].respawn -= dt
                if pickups[i].respawn <= 0, !isOccupied(pickups[i].pos,
                                                        radius: radius) {
                    pickups[i].active = true
                }
                continue
            }
            pickups[i].spin += dt * 2.2
            pickups[i].bob += dt * 3.0
            for c in cars where !c.finished {
                let d = SIMD2(c.pos.x - pickups[i].pos.x,
                              c.pos.z - pickups[i].pos.z)
                if simd_length_squared(d) < radius * radius {
                    pickups[i].active = false
                    pickups[i].respawn = 7
                    c.fuel = min(c.def.fuelCapacity, c.fuel + 40)
                    if c.isPlayer {
                        onPickup?(pickups[i].pos)
                        push(String(localized: "+ fuel"), .good)
                    }
                    break
                }
            }
        }

        // --- boost pads
        //
        // A pad fires once per visit. The old guard was `boost < 1.1`, which
        // is a property of the clock rather than of the pad: boost is set to
        // 1.7 and burns down at 1 per second, so a car that stopped on a pad
        // - spun out, or nudged off the line - was handed a fresh turbo every
        // 0.6 s for as long as it sat there. Remembering which pad last fired
        // and re-arming it when the car drives off is exact, and the pads are
        // never closer together than 100 m, so back-to-back pads still both
        // count.
        for (i, pad) in boostPads.enumerated() {
            for c in cars where !c.finished {
                let d = SIMD2(c.pos.x - pad.pos.x, c.pos.z - pad.pos.z)
                if simd_length_squared(d) < 2.4 * 2.4 {
                    guard c.lastBoostPad != i else { continue }
                    c.lastBoostPad = i
                    c.boost = 1.7
                    if c.isPlayer {
                        onBoost?(pad.pos)
                        push(String(localized: "TURBO!"), .good)
                    }
                } else if c.lastBoostPad == i {
                    c.lastBoostPad = -1     // clear of it again, so re-armed
                }
            }
        }

        // --- slipstream
        for c in cars {
            var best: Float = 0
            for o in cars where o !== c {
                let ds = forwardDistance(from: c.proj.s, to: o.proj.s,
                                         length: track.length)
                guard ds > 1.5, ds < 14 else { continue }
                let dl = abs(o.proj.lateral - c.proj.lateral)
                guard dl < 3.0 else { continue }
                let v = (1 - ds / 14) * (1 - dl / 3.0)
                best = max(best, v)
            }
            c.draft += (best - c.draft) * min(1, dt * 4)
        }

        // --- laps
        for c in cars where !c.finished {
            let half = track.length * 0.5
            if !c.passedHalf {
                let d = c.proj.s - track.file.startS
                let rel = d < 0 ? d + track.length : d
                if rel > half * 0.9 && rel < half * 1.5 { c.passedHalf = true }
            } else {
                let d = c.proj.s - track.file.startS
                let rel = d < 0 ? d + track.length : d
                if rel < track.length * 0.12 {
                    // crossing the finish line = a completed lap
                    c.passedHalf = false
                    let t = raceTime - c.lapStartTime
                    c.lapStartTime = raceTime
                    c.lapTimes.append(t)
                    c.bestLap = min(c.bestLap, t)
                    c.lap += 1
                    if c.isPlayer {
                        lastLapTime = t
                        lapFlash = raceTime
                        playerBestLap = c.bestLap
                    }
                    if c.lap >= totalLaps {
                        c.finished = true
                        c.finishTime = raceTime
                        if c.isPlayer { endRace() }
                    } else if c.isPlayer {
                        onLap?()
                        if c.lap == totalLaps - 1 {
                            push(String(localized: "FINAL LAP!"), .warn)
                        } else {
                            push(String(localized: "Lap \(c.lap + 1)/\(totalLaps)"),
                                 .info)
                        }
                    }
                }
            }
        }

        // --- standings
        let sorted = cars.enumerated().sorted { a, b in
            if a.element.finished != b.element.finished {
                return a.element.finished
            }
            if a.element.finished && b.element.finished {
                return a.element.finishTime < b.element.finishTime
            }
            return a.element.progress > b.element.progress
        }
        for (rank, item) in sorted.enumerated() {
            item.element.position = rank + 1
        }

        // every car home -> race over
        if phase == .racing && cars.allSatisfy({ $0.finished }) {
            phase = .finished
        }

        checkPlayerStuck(dt)
    }

    /// A player out of fuel (or hopelessly lost) must not stay in the race forever.
    private func checkPlayerStuck(_ dt: Float) {
        guard phase == .racing, !player.finished else { return }
        let p = player
        // The fuse starts when the tank runs dry, not when the car finally
        // comes to rest. Rolling resistance is gentle by design, so a car
        // that ran out at racing speed glided for the better part of a
        // minute before it counted as stalled - three quarters of a minute
        // of a banner telling the player something they can do nothing
        // about. Twelve seconds is still a couple of hundred metres of
        // coasting, which on these circuits passes several canisters, and any
        // one of them resets the fuse.
        if p.fuel <= 0 {
            stalledTimer += dt
            if stalledTimer > 2.5 && !dnfWarned {
                dnfWarned = true
                push(String(localized: "Out of fuel — you can't go on"), .warn)
            }
            if stalledTimer > 12 { endRace() }
        } else {
            stalledTimer = 0
            dnfWarned = false
        }

        if cars.allSatisfy({ $0.isPlayer || $0.finished }) {
            allOthersDoneTimer += dt
            if allOthersDoneTimer > 18 { endRace() }
        } else {
            allOthersDoneTimer = 0
        }
    }

    /// The race is over for the player - whether they crossed the line or ran
    /// out of road. The standings are frozen here and the controller is told
    /// once: the guard is what stops a player who retires and then coasts
    /// over the line from firing the finish a second time and pushing the
    /// results screen back another two and a half seconds.
    private func endRace() {
        guard phase == .racing else { return }
        phase = .finished
        snapshotStandings()
        onFinish?()
    }

    /// Gentle rubber-banding so the race stays exciting.
    ///
    /// Only while the player is still in the race. It is measured against
    /// *their* progress, and once they are out of it - home, or stopped on
    /// the road out of fuel - that figure stands still while the field
    /// drives away from it, so every remaining car reads as a runaway
    /// leader and is held to 95.5 % for the whole of the catch-up. The
    /// times the results table then prints are some four per cent slower
    /// than the ones the same race would have produced live.
    ///
    /// Internal rather than private so `RaceEngineTests` can hold it to that
    /// without having to read finishing times back out of a whole race.
    func rubberBand(for car: CarSim) -> Float {
        guard phase == .racing, !player.finished else { return 1.0 }
        let p = player
        let gap = car.progress - p.progress
        if gap > 40 { return 0.955 }        // the leader eases off a little
        if gap < -40 { return 1.055 }       // the stragglers push harder
        return 1.0
    }

    private func push(_ text: String, _ kind: RaceMessage.Kind) {
        // never let the same message hang on screen twice
        let recent = messages.contains { $0.text == text
            && messageClock - $0.born < 2.2 }
        if recent { return }
        messages.append(RaceMessage(text: text, kind: kind,
                                    born: messageClock))
        if messages.count > 3 { messages.removeFirst() }
    }

    // ------------------------------------------------------------ HUD

    private var lastReportedPosition = 0
    private var lastPositionReportAt: Double = -10
    /// Whether the low-fuel warning has already been shown for this dry spell.
    private var lowFuelWarned = false

    private func updateVisualsState(_ dt: Float) {
        let p = player
        if phase == .racing && !p.finished {
            if lastReportedPosition == 0 { lastReportedPosition = p.position }
            else if p.position < lastReportedPosition {
                // stay quiet for a moment after an overtake so the messages
                // do not flip back and forth
                if raceTime - lastPositionReportAt > 2.6 {
                    push(p.position == 1
                         ? String(localized: "IN THE LEAD!")
                         : String(localized: "Position \(p.position)!"), .good)
                    lastPositionReportAt = raceTime
                }
                lastReportedPosition = p.position
            } else if p.position > lastReportedPosition {
                lastReportedPosition = p.position
            }
        }
        playerSpeed = p.speedKph
        playerFuel = p.fuel / p.def.fuelCapacity
        playerBoost = p.boost
        playerLap = min(totalLaps, p.lap + 1)
        playerPosition = p.position
        playerOffTrack = p.offTrack
        // Warn once as the tank runs down, not every frame it stays low: the
        // flag has to mean "already said so", which `playerOutOfFuel` (the
        // tank being empty) does not. It is armed again after a refuel, so a
        // second dry spell is still announced.
        let low = playerFuel < RaceEngine.lowFuelFraction
        if low && !lowFuelWarned && phase == .racing {
            lowFuelWarned = true
            push(String(localized: "Running out of fuel!"), .warn)
        } else if playerFuel > RaceEngine.refuelledFraction {
            lowFuelWarned = false
        }
        playerOutOfFuel = p.fuel <= 0
        wrongWay = phase == .racing && !p.finished && p.speed > 3
            && simd_dot(p.vel, SIMD2(track.tangents[p.proj.index].x,
                                     track.tangents[p.proj.index].y)) < -2
    }

    /// After the player finishes, simulate the opponents to the end so the
    /// results table is complete instead of showing half the field as DNF.
    ///
    /// Three things decide how long that takes, and the flat 90 s budget this
    /// used to run on got all three wrong. A race is 132 s on Sunset, 141 on
    /// Cove and 159 on Frostpeak, so 90 s could not finish one even from the
    /// start line: a player who ran dry on the first lap was shown a results
    /// table with six of the eight cars marked DNF - precisely the "the game
    /// is broken" reading this exists to prevent. The budget is therefore
    /// taken from the distance actually left to cover, priced at a pace no
    /// car drops below while it is still moving. It is capped as well,
    /// because a field that has genuinely stopped would otherwise spend the
    /// whole of it discovering that - hence the stall check too.
    ///
    /// Nobody watches this simulation and only its finishing order is read
    /// back out of it, so it runs at half the live rate. The forces are mild
    /// enough at 1/60 that lap times move by hundredths.
    ///
    /// Slicing: eighteen thousand fixed steps is the worst case (a retirement
    /// on the first lap of the longest circuit) and it measured 145 ms in an
    /// optimised build - a freeze at the exact moment the results screen
    /// slides in. `catchUpToFinish` therefore runs the same steps a few
    /// milliseconds at a time and yields between the slices; the race is
    /// stopped by then, so nothing on screen moves while it works.
    /// `fastForwardToFinish` is the same thing in one go, for the tests.
    func fastForwardToFinish(maxSeconds: Double? = nil) {
        beginCatchUp(maxSeconds: maxSeconds)
        while !advanceCatchUp(maxSteps: 2_000) { }
    }

    /// The catch-up in slices, with the main thread let go of between them.
    func catchUpToFinish() async {
        beginCatchUp(maxSeconds: nil)
        // ~240 steps is a couple of milliseconds' work at the rate measured
        // above - short enough that a slice never shows up as a dropped frame.
        while !advanceCatchUp(maxSteps: 240) { await Task.yield() }
    }

    /// Everything a sliced catch-up has to remember between its slices,
    /// including the callbacks it has silenced: no pickup chimes or crash
    /// sounds from a race nobody is watching.
    private struct CatchUp {
        var budget: Double
        var elapsed: Double = 0
        var outstanding: Float
        var stalled: Double = 0
        var onPickup: ((SIMD3<Float>) -> Void)?
        var onBoost: ((SIMD3<Float>) -> Void)?
        var onLap: (() -> Void)?
        var onCrash: ((Float) -> Void)?
        var onFinish: (() -> Void)?
    }
    private var catchUp: CatchUp?

    /// The total distance the field still owes. `progress` is measured from
    /// the finish line for every car, grid offset included, so `laps *
    /// length` is exactly where each of them is done and the subtraction
    /// needs no correction for where a car happened to start.
    ///
    /// It only ever falls - a car that finishes drops out of the sum entirely
    /// - so it stands still if and only if nobody is moving. Watching the
    /// leader's progress instead does not work: the moment the leader crosses
    /// the line, the set the figure is drawn from loses its best member, the
    /// figure jumps backwards, and the whole thing reads as a stall.
    private func outstandingDistance() -> Float {
        let finish = Float(totalLaps) * track.length
        return cars.filter { !$0.finished && !$0.isPlayer }
            .reduce(0) { $0 + max(0, finish - $1.progress) }
    }

    private func beginCatchUp(maxSeconds: Double?) {
        guard catchUp == nil else { return }
        let finish = Float(totalLaps) * track.length
        // How far the car with the most left to do still has to drive. 9 m/s
        // is comfortably under the slowest car's pace on the tightest
        // circuit, so the budget is generous rather than tight.
        let furthestBack = cars
            .filter { !$0.finished && !$0.isPlayer }
            .map { max(0, finish - $0.progress) }
            .max() ?? 0
        catchUp = CatchUp(
            budget: maxSeconds ?? min(300, Double(furthestBack / 9) + 15),
            outstanding: outstandingDistance(),
            onPickup: onPickup, onBoost: onBoost, onLap: onLap,
            onCrash: onCrash, onFinish: onFinish)
        onPickup = nil; onBoost = nil; onLap = nil
        onCrash = nil; onFinish = nil
    }

    /// One slice: at most `maxSteps` fixed steps. Returns true once the field
    /// is home (or out of budget), at which point the callbacks are back and
    /// the standings are settled. Slicing changes only *when* the steps run,
    /// never what they compute, so the order it produces is the same either
    /// way - `RaceEngineTests` holds it to that.
    @discardableResult
    private func advanceCatchUp(maxSteps: Int) -> Bool {
        guard var c = catchUp else { return true }
        let step = Double(catchUpDT)
        var done = false
        var n = 0
        // the player no longer matters - we only complete the opponents' times
        while n < maxSteps {
            guard c.elapsed < c.budget,
                  !cars.allSatisfy({ $0.finished || $0.isPlayer })
            else { done = true; break }
            raceTime += step
            simulate(catchUpDT)
            c.elapsed += step
            n += 1
            let now = outstandingDistance()
            if now < c.outstanding - 1 {
                c.outstanding = now
                c.stalled = 0
            } else {
                // Out of fuel and going nowhere: they are DNF, and no amount
                // of budget is going to change that.
                c.stalled += step
                if c.stalled > 8 { done = true; break }
            }
        }
        guard done else { catchUp = c; return false }
        onPickup = c.onPickup; onBoost = c.onBoost; onLap = c.onLap
        onCrash = c.onCrash; onFinish = c.onFinish
        catchUp = nil
        snapshotStandings()
        return true
    }

    func snapshotStandings() {
        let order = cars.sorted { a, b in
            if a.finished != b.finished { return a.finished }
            if a.finished && b.finished { return a.finishTime < b.finishTime }
            return a.progress > b.progress
        }
        finalOrder = order
        finalStandings = order.map {
            Standing(name: displayNames.indices.contains($0.slot)
                        ? displayNames[$0.slot] : $0.def.name,
                     color: $0.def.paint,
                     time: $0.finishTime, finished: $0.finished,
                     isPlayer: $0.isPlayer)
        }
        // The placing comes out of the table the player is shown, so the
        // headline and the standings cannot disagree. Reading `position` off
        // the car instead took it from the previous step's sort - the one
        // before the finish it was reporting - so two cars crossing the line
        // in the same step could put a winner's screen at second place.
        if let i = order.firstIndex(where: { $0.isPlayer }) {
            playerFinalPosition = i + 1
        }
    }

    func setPlayerInput(_ i: DriverInput) {
        guard phase == .racing || phase == .finished else {
            player.input = DriverInput(throttle: 0, steer: i.steer,
                                       handbrake: false)
            return
        }
        if player.finished { return }
        player.input = i
    }

    var playerCar: CarSim { player }
}
