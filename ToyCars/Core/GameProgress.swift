//
//  GameProgress.swift
//  ToyCars
//
//  The player's saved progress. Fully offline, one JSON in Application Support.
//

import Foundation
import Observation
import OSLog

struct TrackRecord: Codable, Sendable {
    /// `nil` until the circuit has actually been finished once.
    ///
    /// These used to hold `.infinity` as their "no time yet" value, which
    /// `JSONEncoder` refuses to write. A retirement stores the default
    /// record, so the encode threw, `save()` swallowed it with `try?`, and
    /// the profile silently stopped being written for the rest of the
    /// session - the coins from that race, any car bought after it, every
    /// setting and every unlock. A missing value has to be *missing*, not a
    /// number the format cannot represent.
    var bestLap: Double? = nil
    var bestTotal: Double? = nil
    var bestPosition: Int = 0
    var completed: Bool = false
}

struct ProgressState: Codable, Sendable {
    /// The two cars a new profile comes with. Both are priced at 0 in
    /// `CarDef.prices`, so this and the price list have to agree.
    static let starterCars: Set<String> = ["bumble", "chili"]

    var coins: Int = 250
    var ownedCars: Set<String> = ProgressState.starterCars
    var selectedCar: String = "bumble"
    var records: [String: TrackRecord] = [:]
    var soundOn: Bool = true
    var musicOn: Bool = true
    var steerMode: Int = 0          // 0 = buttons, 1 = finger drag
    var seenTutorial: Bool = false
    /// Optional for the same reason as `trackVersion` below: a synthesised
    /// decoder does not fall back to a default for a key that is not there,
    /// so a profile written before this setting existed has to be able to
    /// decode without it.
    var hapticsOn: Bool? = nil
    /// Bumped whenever the tracks are rebuilt with a different shape. Times
    /// set on an older layout are not comparable, so they get dropped on
    /// load. Optional, so a save written before this field existed still
    /// decodes instead of resetting the whole profile.
    var trackVersion: Int? = nil

    /// What the game should actually do, with the default filled in.
    var hapticsEnabled: Bool { hapticsOn ?? true }

    /// Puts back the one thing the rest of the game takes for granted: that
    /// the selected car is a car the player owns. Returns whether anything
    /// had to be mended.
    ///
    /// A profile is a file, and a file outlives the build that wrote it. This
    /// one is untrusted input - an older version with a different idea of
    /// what `select` was allowed to do, a copy carried over from another
    /// device, a hand-edited debug profile - and `selectedCar` is the field
    /// with teeth: `RaceController.setup` hands it straight to the catalogue
    /// and races whatever comes back. A profile that had `sprout` selected
    /// and only the two starters owned put a car the player has not bought
    /// on the grid, and the garage showed the padlock and the price beside
    /// the "in use" badge on the same car.
    ///
    /// Nothing in the current game can produce that state - `select` refuses
    /// a car that is not owned and `unlockForDebug` adds it before selecting
    /// it - which is exactly why it has to be repaired here rather than
    /// guarded for at every reader. This is the one place the invariant is
    /// established; after it, `selectedCar` is owned by definition.
    mutating func repairInvariants() -> Bool {
        var mended = false
        if ownedCars.isEmpty {
            ownedCars = ProgressState.starterCars
            mended = true
        }
        if !ownedCars.contains(selectedCar) {
            // The cheapest car actually owned, in showroom order, so the
            // fallback is the least surprising one on the shelf.
            selectedCar = CarCatalog.order.first { ownedCars.contains($0) }
                ?? ownedCars.sorted()[0]
            mended = true
        }
        return mended
    }
}

@MainActor
@Observable
final class GameProgress {
    static let shared = GameProgress()

    /// 1 = the original flat 830 m circuits, 2 = the long hill layouts.
    static let trackVersion = 2

    private(set) var state: ProgressState

    /// The file this profile is backed by. Injectable so a test can be given
    /// a throwaway one: the suite used to run against `shared`, which is the
    /// player's own profile on disk, so every run left another record and
    /// another few hundred coins behind in it for good.
    private let fileURL: URL

    /// Whether writing to that file is allowed at all.
    ///
    /// A debug launch is a *look* at the game, not a session of playing it,
    /// and the switches below only ever changed `state` in memory - which was
    /// exactly as far as the promise went. The first legitimate `save()` after
    /// one of them, and finishing a race is enough, wrote the whole of `state`
    /// out including the debug part: `-race` marked the tutorial seen for
    /// good, `-car` and `-rich` handed over cars and coins, and a race
    /// shortened with `-laps 1` set a lap record on a full-length circuit that
    /// nobody can ever beat. Suppressing the write is the only version of that
    /// promise the file cannot get around.
    private(set) var writesSuppressed = false

    /// The switches that make a launch a debug launch. `-laps` is here
    /// because a shortened race produces times that are not comparable with
    /// the ones a real race sets, not because it changes the profile itself.
    static let debugArguments = ["-screen", "-race", "-car", "-rich",
                                 "-laps", "-nofuel", "-autopilot",
                                 "-steertest", "-tut"]

    /// Not private: it is the default argument of an internal initialiser.
    static var defaultFileURL: URL {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory,
                                           in: .userDomainMask)[0]
            .appendingPathComponent("ToyCars", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir,
                                                 withIntermediateDirectories: true)
        return dir.appendingPathComponent("progress.json")
    }

    /// `suppressWrites` defaults to whatever the command line says, so the
    /// app never has to remember to ask; a test passes it explicitly.
    init(fileURL: URL = GameProgress.defaultFileURL,
         suppressWrites: Bool? = nil) {
        self.fileURL = fileURL
        self.writesSuppressed = suppressWrites
            ?? GameProgress.debugArguments.contains {
                ProcessInfo.processInfo.arguments.contains($0)
            }
        if let data = try? Data(contentsOf: fileURL),
           let s = try? JSONDecoder().decode(ProgressState.self, from: data) {
            state = s
        } else {
            state = ProgressState()
        }
        // Before anything reads it - see `repairInvariants`.
        var dirty = state.repairInvariants()
        if state.trackVersion != GameProgress.trackVersion {
            // The tracks were rebuilt: lap and race times from the previous
            // layout would stand as records nobody can beat. Placings and
            // unlocks are kept - those were earned.
            for (id, rec) in state.records {
                var r = rec
                r.bestLap = nil
                r.bestTotal = nil
                state.records[id] = r
            }
            state.trackVersion = GameProgress.trackVersion
            dirty = true
        }
        if dirty { save() }
    }

    private static let log = Logger(subsystem: "cz.rob.ToyCars",
                                    category: "progress")

    func save() {
        // A debug launch keeps everything it does to itself - see
        // `writesSuppressed`. The migration above is idempotent, so skipping
        // its write costs nothing: the next normal launch does it again.
        guard !writesSuppressed else { return }
        do {
            let data = try JSONEncoder().encode(state)
            try data.write(to: fileURL, options: .atomic)
        } catch {
            // Deliberately not `try?`. This is the player's coins and their
            // collection: a write that cannot happen must leave a trace
            // rather than look exactly like a write that did.
            GameProgress.log.error(
                "progress could not be saved: \(String(describing: error), privacy: .public)")
        }
    }

    // ------------------------------------------------------------ queries

    func owns(_ carID: String) -> Bool { state.ownedCars.contains(carID) }

    func canBuy(_ car: CarDef) -> Bool {
        !owns(car.id) && state.coins >= car.price
    }

    func buy(_ car: CarDef) {
        guard canBuy(car) else { return }
        state.coins -= car.price
        state.ownedCars.insert(car.id)
        save()
    }

    func select(_ carID: String) {
        guard owns(carID) else { return }
        state.selectedCar = carID
        save()
    }

    /// A track is unlocked once the player has completed every earlier one.
    func isUnlocked(_ track: TrackDef) -> Bool {
        if track.unlock == 0 { return true }
        let earlier = TrackCatalog.shared.tracks.filter { $0.unlock < track.unlock }
        return earlier.allSatisfy { record($0.id).completed }
    }

    func record(_ trackID: String) -> TrackRecord {
        state.records[trackID] ?? TrackRecord()
    }

    struct RaceOutcome {
        var position: Int
        var totalTime: Double
        var bestLap: Double
        var finished: Bool
        var coins: Int
        var newBestLap: Bool
        var newBestTotal: Bool
        var unlockedTrack: TrackDef?
    }

    /// Keeps `time` if it beats what is on record, and refuses anything that
    /// is not a real time at all - the engine's "no lap yet" value is
    /// `.infinity`, and letting one of those through is what used to make the
    /// whole profile unwritable.
    private static func improve(_ best: inout Double?,
                                with time: Double) -> Bool {
        guard time.isFinite, time > 0 else { return false }
        guard let current = best else { best = time; return true }
        guard time < current else { return false }
        best = time
        return true
    }

    func apply(trackID: String, position: Int, totalTime: Double,
               bestLap: Double, finished: Bool, fieldSize: Int) -> RaceOutcome {
        var rec = record(trackID)
        let wasCompleted = rec.completed
        var newLap = false, newTotal = false
        if finished {
            newLap = GameProgress.improve(&rec.bestLap, with: bestLap)
            newTotal = GameProgress.improve(&rec.bestTotal, with: totalTime)
            if rec.bestPosition == 0 || position < rec.bestPosition {
                rec.bestPosition = position
            }
            if position <= 3 { rec.completed = true }
        }
        state.records[trackID] = rec

        let base = TrackCatalog.shared.track(trackID)?.reward ?? 100
        var coins = 0
        if finished {
            let placeBonus = max(0, fieldSize - position + 1)
            coins = base / 4 + base * placeBonus / (fieldSize + 1)
            if position == 1 { coins += base / 2 }
            if newLap { coins += 40 }
        } else {
            coins = base / 8
        }
        state.coins += coins

        var unlocked: TrackDef?
        if rec.completed && !wasCompleted {
            unlocked = TrackCatalog.shared.tracks.first {
                !isUnlockedBefore($0, excluding: trackID) && isUnlocked($0)
            }
        }
        save()
        return RaceOutcome(position: position, totalTime: totalTime,
                           bestLap: bestLap, finished: finished, coins: coins,
                           newBestLap: newLap, newBestTotal: newTotal,
                           unlockedTrack: unlocked)
    }

    private func isUnlockedBefore(_ track: TrackDef, excluding id: String) -> Bool {
        if track.unlock == 0 { return true }
        let earlier = TrackCatalog.shared.tracks.filter { $0.unlock < track.unlock }
        return earlier.allSatisfy { $0.id == id ? false : record($0.id).completed }
    }

    /// Debug only, driven by command line arguments. A debug launch must not
    /// change what the player sees the next time they open the game normally,
    /// and none of these can: every one of the switches that reaches them is
    /// in `debugArguments`, so `save()` is already suppressed for the whole
    /// session by the time they run - not only for the call itself, which is
    /// as far as this used to go.
    /// The id comes off the command line, so it is checked against the
    /// catalogue: `-car nosuchcar` used to own and select a car that does not
    /// exist, which left the garage on a different car from the one the race
    /// then used.
    func unlockForDebug(_ carID: String) {
        guard CarCatalog.shared.car(carID) != nil else { return }
        state.ownedCars.insert(carID)
        state.selectedCar = carID
    }

    /// `-race` and `-screen` go straight past the intro tutorial.
    func skipTutorialForDebug() { state.seenTutorial = true }

    func grantForDebug(coins: Int) {
        state.coins = coins
    }

    func setSound(_ on: Bool) { state.soundOn = on; save() }
    func setMusic(_ on: Bool) { state.musicOn = on; save() }
    func setHaptics(_ on: Bool) {
        state.hapticsOn = on
        Haptics.enabled = on
        save()
    }
    func setSteerMode(_ m: Int) { state.steerMode = m; save() }
    func markTutorialSeen() { state.seenTutorial = true; save() }
}
