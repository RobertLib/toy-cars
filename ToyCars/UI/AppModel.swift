//
//  AppModel.swift
//  ToyCars
//
//  Screen switching and passing race results around.
//

import Foundation
import Observation
import SwiftUI

@MainActor
@Observable
final class AppModel {
    enum Screen: Equatable {
        case menu
        case garage
        case tracks
        case race(String)          // track id
        case results
    }

    var screen: Screen = .menu
    var lastOutcome: GameProgress.RaceOutcome?
    var lastTrackID: String = ""
    var lastFinalOrder: [RaceEngine.Standing] = []
    var loadingProgress: Double = 0
    var showSettings = false
    /// Set by `-screen tracks -track <n>` so a particular circuit - a locked
    /// one, say - can be inspected without tapping through to it.
    var debugTrackIndex: Int?

    let progress = GameProgress.shared

    func go(_ s: Screen) {
        withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) {
            screen = s
        }
    }

    func startRace(track: TrackDef) {
        lastTrackID = track.id
        go(.race(track.id))
    }
}

// ---------------------------------------------------------------- debug

extension AppModel {
    /// The model the app starts on, with the debug switches applied.
    ///
    /// This lives here rather than in `RootView.init` because a `View` is a
    /// value the system is free to build again whenever it likes, and the
    /// old version put the whole of this in that initialiser - so every
    /// rebuild made throwaway models and, worse, `markTutorialSeen()` wrote
    /// to the profile on disk. One debug launch switched the intro tutorial
    /// off permanently. Nothing here touches the saved file.
    static func launch() -> AppModel {
        let model = AppModel()
        let args = ProcessInfo.processInfo.arguments

        // The switches that hand something out are applied first and for
        // every launch, because they say nothing about *where* the app should
        // open and so have no business being nested inside the ones that do.
        // They used to be: `-rich` was read only inside the `-screen` branch
        // and `-car` only inside the `-race` one, and `-screen` returns
        // early - so `-rich` on its own left the player on 250 coins,
        // `-screen garage -car neo` opened the garage on whatever car the
        // profile already had, and `-race sunset -rich` handed over nothing.
        // Every one of those still cost the session its writes, since the
        // mere presence of the switch sets `GameProgress.writesSuppressed`:
        // the promise was broken in both directions at once.
        if let i = args.firstIndex(of: "-car"), i + 1 < args.count {
            model.progress.unlockForDebug(args[i + 1])
        }
        if args.contains("-rich") {
            model.progress.grantForDebug(coins: 5000)
        }

        if let i = args.firstIndex(of: "-screen"), i + 1 < args.count {
            model.progress.skipTutorialForDebug()
            switch args[i + 1] {
            case "garage": model.screen = .garage
            case "tracks":
                model.screen = .tracks
                if let j = args.firstIndex(of: "-track"), j + 1 < args.count,
                   let n = Int(args[j + 1]) { model.debugTrackIndex = n }
            case "results":
                var place = 1
                if let j = args.firstIndex(of: "-pos"), j + 1 < args.count,
                   let n = Int(args[j + 1]) { place = n }
                model.screen = .results
                model.fabricateOutcomeForDebug(position: place)
            case "settings":
                model.screen = .menu
                model.showSettings = true
            default: model.screen = .menu
            }
            return model
        }

        // `-race sunset` jumps straight into a race.
        if let i = args.firstIndex(of: "-race"), i + 1 < args.count {
            let id = args[i + 1]
            model.lastTrackID = id
            model.screen = .race(id)
            // skip the tutorial on a debug launch (unless -tut is given)
            if !args.contains("-tut") { model.progress.skipTutorialForDebug() }
        }
        return model
    }

    /// Fills in a plausible race so the results screen can be opened directly
    /// with `-screen results [-pos n]`, without driving one. A position past
    /// the end of the field stands for a retirement, which is the one outcome
    /// with a layout of its own.
    func fabricateOutcomeForDebug(position: Int) {
        let track = TrackCatalog.shared.tracks.first
        lastTrackID = track?.id ?? ""
        // Eight, because that is the size of the field a real race runs.
        let names = ["Bolt", "Bumble", "Chili", "Neo",
                     "Rocky", "Scoop", "Sprout", "Frosty"]
        let colors = ["#39e6ff", "#ffc21c", "#ee2f45", "#8b5cf6",
                      "#7cd94f", "#ff7a2f", "#22d3a5", "#35a7f2"]
        let field = names.count
        let retired = position > field
        let place = retired ? field : max(1, min(field, position))
        lastFinalOrder = (0..<field).map { i in
            let isPlayer = i == place - 1
            return RaceEngine.Standing(
                name: isPlayer ? "You" : names[i],
                color: colors[i],
                time: 78.4 + Double(i) * 2.35,
                finished: !(isPlayer && retired) && i < field - 1,
                isPlayer: isPlayer)
        }
        lastOutcome = GameProgress.RaceOutcome(
            position: place,
            totalTime: retired ? 0 : 78.4 + Double(place - 1) * 2.35,
            bestLap: retired ? .infinity : 25.312,
            finished: !retired,
            coins: retired ? 30 : 240,
            newBestLap: !retired,
            newBestTotal: !retired,
            unlockedTrack: retired ? nil
                : TrackCatalog.shared.tracks.dropFirst().first)
    }
}
