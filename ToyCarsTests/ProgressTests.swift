//
//  ProgressTests.swift
//  ToyCarsTests
//
//  The saved profile. These exist because the one thing that can go wrong
//  here goes wrong silently: `save()` cannot report a failure to the player,
//  so nothing but a test will notice that the file stopped being written.
//

import Foundation
import Testing
@testable import ToyCars

@MainActor
struct ProgressTests {

    /// The regression this file exists for.
    ///
    /// `TrackRecord`'s times used to default to `.infinity`, and a retirement
    /// stores that default record untouched - so the encode threw, `save()`
    /// swallowed it with `try?`, and from the first DNF on an uncleared
    /// circuit nothing was written to disk again for the rest of the session:
    /// the coins from that race, any car bought after it, every setting and
    /// every unlock. Whatever a record holds, the profile has to encode.
    @Test func aRetirementLeavesAWritableProfile() throws {
        var state = ProgressState()
        state.trackVersion = GameProgress.trackVersion
        // exactly what `apply(finished: false)` puts in for a circuit with no
        // record of its own yet
        state.records["sunset"] = TrackRecord()

        let data = try JSONEncoder().encode(state)
        let back = try JSONDecoder().decode(ProgressState.self, from: data)
        #expect(back.records["sunset"]?.bestLap == nil)
        #expect(back.coins == state.coins)
    }

    /// Every state the game can actually reach has to survive the round trip,
    /// including one with a real time on record and one straight off the
    /// version migration.
    @Test func everyReachableProfileEncodes() throws {
        var state = ProgressState()
        state.records["sunset"] = TrackRecord(bestLap: 25.3, bestTotal: 78.4,
                                              bestPosition: 1, completed: true)
        state.records["cove"] = TrackRecord()
        state.records["frost"] = TrackRecord(bestLap: nil, bestTotal: nil,
                                             bestPosition: 5, completed: false)
        state.ownedCars = ["bumble", "chili", "neo"]
        state.hapticsOn = false
        let data = try JSONEncoder().encode(state)
        let back = try JSONDecoder().decode(ProgressState.self, from: data)
        #expect(back.records.count == 3)
        #expect(back.records["sunset"]?.bestLap == 25.3)
        #expect(back.records["frost"]?.bestPosition == 5)
        #expect(back.hapticsEnabled == false)
    }

    /// A profile written before these fields were optional still decodes, and
    /// one written before they existed at all decodes too - the times simply
    /// come back as "no record".
    @Test func olderProfilesStillDecode() throws {
        let withTimes = """
        {"coins":610,"ownedCars":["bumble"],"selectedCar":"bumble",
         "records":{"sunset":{"bestLap":24.5,"bestTotal":75.2,
                              "bestPosition":2,"completed":true}},
         "soundOn":true,"musicOn":false,"steerMode":1,"seenTutorial":true,
         "trackVersion":2}
        """
        let a = try JSONDecoder().decode(ProgressState.self,
                                         from: Data(withTimes.utf8))
        #expect(a.coins == 610)
        #expect(a.records["sunset"]?.bestLap == 24.5)
        #expect(a.records["sunset"]?.completed == true)

        let withoutTimes = """
        {"coins":250,"ownedCars":["bumble","chili"],"selectedCar":"chili",
         "records":{"cove":{"bestPosition":0,"completed":false}},
         "soundOn":true,"musicOn":true,"steerMode":0,"seenTutorial":false}
        """
        let b = try JSONDecoder().decode(ProgressState.self,
                                         from: Data(withoutTimes.utf8))
        #expect(b.records["cove"]?.bestLap == nil)
        #expect(b.trackVersion == nil)
        #expect(b.hapticsEnabled == true)
    }

    /// A profile of its own, in a file nothing else will ever look at.
    ///
    /// These tests used to run against `GameProgress.shared`, which is the
    /// player's real profile: `apply` writes it straight to disk, so every
    /// run of the suite left another record and another few hundred coins
    /// behind in the app's own save file, for ever. Working around that with
    /// unique scratch track ids kept the runs from colliding with each other
    /// but still wrote to the file, and the records only accumulated. A
    /// throwaway file is the actual fix, and it lets a test use the real
    /// circuit ids - which is what the unlock chain is expressed in.
    private func scratchProgress() -> GameProgress {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("toycars-test-\(UUID().uuidString).json")
        return GameProgress(fileURL: url, suppressWrites: false)
    }

    /// `.infinity` is the engine's "no lap yet" value and it must not be able
    /// to get from a race into the profile, whatever the caller passes.
    @Test func noLapTimeNeverReachesTheRecord() throws {
        let p = scratchProgress()
        _ = p.apply(trackID: "sunset", position: 1, totalTime: .infinity,
                    bestLap: .infinity, finished: true, fieldSize: 8)
        let rec = p.record("sunset")
        #expect(rec.bestLap == nil)
        #expect(rec.bestTotal == nil)
        // and the profile as a whole is still writable
        #expect(throws: Never.self) { try JSONEncoder().encode(p.state) }
    }

    /// A better time replaces the record, a worse one does not, and the first
    /// time of all is always an improvement.
    @Test func onlyBetterTimesAreKept() throws {
        let p = scratchProgress()
        let first = p.apply(trackID: "sunset", position: 1, totalTime: 90,
                            bestLap: 30, finished: true, fieldSize: 8)
        #expect(first.newBestLap)
        #expect(p.record("sunset").bestLap == 30)

        let worse = p.apply(trackID: "sunset", position: 1, totalTime: 95,
                            bestLap: 32, finished: true, fieldSize: 8)
        #expect(!worse.newBestLap)
        #expect(p.record("sunset").bestLap == 30)

        let better = p.apply(trackID: "sunset", position: 1, totalTime: 88,
                             bestLap: 28.5, finished: true, fieldSize: 8)
        #expect(better.newBestLap)
        #expect(p.record("sunset").bestLap == 28.5)
        #expect(p.record("sunset").bestTotal == 88)
    }

    /// A scratch profile really is scratch: nothing it does reaches the file
    /// the player's own profile lives in. This is the guard on the fix above
    /// - without it the suite would quietly go back to banking coins into the
    /// save file the moment someone reached for `shared` again.
    @Test func aTestProfileNeverTouchesThePlayersOwn() throws {
        let before = GameProgress.shared.state
        let p = scratchProgress()
        _ = p.apply(trackID: "sunset", position: 1, totalTime: 80,
                    bestLap: 26, finished: true, fieldSize: 8)
        #expect(p.state.coins != before.coins, "the scratch profile paid out")
        #expect(GameProgress.shared.state.coins == before.coins)
        #expect(GameProgress.shared.record("sunset").bestLap
                    == before.records["sunset"]?.bestLap)
    }

    /// The other direction, and the one the change above could plausibly
    /// break: an ordinary profile still writes, and what it wrote is what
    /// comes back. Suppressing the debug case is worth nothing if it also
    /// quietly stopped the real one.
    @Test func anOrdinaryProfileStillReachesTheDisk() throws {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("toycars-disk-\(UUID().uuidString).json")
        let p = GameProgress(fileURL: url, suppressWrites: false)
        _ = p.apply(trackID: "sunset", position: 1, totalTime: 88,
                    bestLap: 28.5, finished: true, fieldSize: 8)
        p.buy(try #require(CarCatalog.shared.car("sprout")))
        #expect(FileManager.default.fileExists(atPath: url.path))

        // Read back through a second instance, exactly as the next launch of
        // the game does.
        let reloaded = GameProgress(fileURL: url, suppressWrites: false)
        #expect(reloaded.record("sunset").bestTotal == 88)
        #expect(reloaded.record("sunset").completed)
        #expect(reloaded.owns("sprout"))
        #expect(reloaded.state.coins == p.state.coins)
        try? FileManager.default.removeItem(at: url)
    }

    /// A debug launch is a look at the game, not a session of playing it.
    /// `-race`, `-rich` and the rest change `state` in memory, and the first
    /// ordinary `save()` after one of them - finishing a race is enough -
    /// used to write the whole lot out: the tutorial marked seen for good,
    /// cars and coins granted, and a race shortened with `-laps 1` leaving a
    /// lap record on a full-length circuit that nobody can beat.
    @Test func aDebugLaunchWritesNothingToDisk() throws {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("toycars-debug-\(UUID().uuidString).json")
        let p = GameProgress(fileURL: url, suppressWrites: true)
        p.skipTutorialForDebug()
        p.grantForDebug(coins: 5000)
        p.unlockForDebug("neo")
        _ = p.apply(trackID: "sunset", position: 1, totalTime: 40,
                    bestLap: 40, finished: true, fieldSize: 8)
        p.setSound(false)
        p.markTutorialSeen()
        #expect(p.writesSuppressed)
        // The in-memory state still behaves normally - that is the point of
        // the switches - but the file was never created at all.
        #expect(p.state.seenTutorial)
        #expect(p.owns("neo"))
        #expect(!FileManager.default.fileExists(atPath: url.path))
    }

    // ------------------------------------------------------- invariants

    /// A profile is a file, and a file outlives the build that wrote it.
    ///
    /// One found on a device had `sprout` selected with only the two starters
    /// owned - and nothing downstream checked: `RaceController.setup` handed
    /// `selectedCar` straight to the catalogue, so the race went out with a
    /// car the player had not bought, and the garage drew the "in use" badge
    /// beside the padlock and the price on the same car. Nothing in the game
    /// can produce that state today, which is exactly why the repair belongs
    /// on the way in rather than in a guard at every reader.
    @Test func aSelectedCarThePlayerDoesNotOwnIsPutBack() throws {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("toycars-bad-\(UUID().uuidString).json")
        var bad = ProgressState()
        bad.trackVersion = GameProgress.trackVersion
        bad.ownedCars = ["bumble", "chili"]
        bad.selectedCar = "sprout"
        try JSONEncoder().encode(bad).write(to: url)

        let p = GameProgress(fileURL: url, suppressWrites: false)
        #expect(p.owns(p.state.selectedCar),
                "loaded a profile racing \(p.state.selectedCar), which is not owned")
        #expect(p.state.selectedCar == "bumble",
                "fell back to \(p.state.selectedCar) rather than the cheapest owned car")
        // Mended on disk too, so the next launch does not have to do it again.
        let reloaded = GameProgress(fileURL: url, suppressWrites: false)
        #expect(reloaded.state.selectedCar == "bumble")
        try? FileManager.default.removeItem(at: url)
    }

    /// The fallback is the cheapest car actually owned, not a hard-coded
    /// `bumble` - a profile that has somehow lost the starters still has to
    /// come up with a car it owns.
    @Test func theFallbackIsACarTheProfileActuallyHas() throws {
        var state = ProgressState()
        state.ownedCars = ["neo", "rocky"]
        state.selectedCar = "bumble"
        // Called outside `#expect`: the macro takes its expression by value,
        // so a mutating member cannot be called inside one.
        let mended = state.repairInvariants()
        #expect(mended)
        #expect(state.ownedCars.contains(state.selectedCar))
        // Rocky comes before Neo in `CarCatalog.order`.
        #expect(state.selectedCar == "rocky")
    }

    /// An empty collection is not a state the game can drive from, so it goes
    /// back to the two free starters rather than leaving the garage blank.
    @Test func anEmptyCollectionGetsTheStartersBack() throws {
        var state = ProgressState()
        state.ownedCars = []
        state.selectedCar = "neo"
        let mended = state.repairInvariants()
        #expect(mended)
        #expect(state.ownedCars == ProgressState.starterCars)
        #expect(state.ownedCars.contains(state.selectedCar))
    }

    /// And the ordinary case is left alone - a repair that fires on a healthy
    /// profile would rewrite the file on every launch.
    @Test func aHealthyProfileIsNotTouched() throws {
        var state = ProgressState()
        state.ownedCars = ["bumble", "chili", "bolt"]
        state.selectedCar = "bolt"
        let mended = state.repairInvariants()
        #expect(!mended)
        #expect(state.selectedCar == "bolt")
    }

    /// Every car the starters name has to be free, or a new profile owns
    /// something it never paid for - or worse, cannot afford to replace.
    @Test func theStartersAreTheFreeCars() throws {
        for id in ProgressState.starterCars {
            let car = try #require(CarCatalog.shared.car(id))
            #expect(car.price == 0, "\(id) is a starter but costs \(car.price)")
        }
    }
}
