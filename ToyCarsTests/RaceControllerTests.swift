//
//  RaceControllerTests.swift
//  ToyCarsTests
//
//  The controller's own state, as opposed to the simulation's. What goes
//  wrong here is a race that will not start rather than one that misbehaves,
//  which is invisible to every test that only drives the engine.
//

import Foundation
import Testing
@testable import ToyCars

@MainActor
struct RaceControllerTests {

    /// The regression this file exists for.
    ///
    /// The pause panel sets `paused` when it opens and only its RESUME button
    /// ever cleared it, so RESTART reloaded the race with the loop still
    /// short-circuited: the new race sat frozen on the grid with the controls
    /// dead, and the only way out was to open the panel again and resume.
    ///
    /// `trackID` is empty on a controller that has never been set up, so
    /// `setup` bails out at once - nothing heavy is loaded and the pause is
    /// all this is about.
    @Test func restartClearsThePause() async throws {
        let c = RaceController()
        c.setPaused(true)
        #expect(c.paused)
        await c.restart(carID: "bumble")
        c.stopLoop()
        #expect(!c.paused, "restart() left the controller paused")
    }

    /// Pausing lets go of everything the player was holding, because a
    /// cancelled gesture never sends its release.
    @Test func pausingReleasesTheControls() throws {
        let c = RaceController()
        c.steerInput = 1
        c.braking = true
        c.drifting = true
        c.setPaused(true)
        #expect(c.steerInput == 0)
        #expect(!c.braking)
        #expect(!c.drifting)
    }

    /// A track that is not in the bundle has to land on the failure screen
    /// rather than on a loading card with no way off it.
    @Test func aMissingTrackFailsTheLoadInsteadOfHanging() async throws {
        let c = RaceController()
        await c.setup(trackID: "no-such-circuit", carID: "bumble",
                      soundOn: false)
        #expect(c.loadFailed)
        #expect(!c.ready)
    }
}
