//
//  RootView.swift
//  ToyCars
//

import SwiftUI

struct RootView: View {
    @State private var app = AppModel.launch()
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        ZStack {
            switch app.screen {
            case .menu:
                MainMenuView(app: app)
                    .transition(.asymmetric(insertion: .opacity,
                                            removal: .opacity))
            case .garage:
                GarageView(app: app)
                    .transition(.move(edge: .trailing).combined(with: .opacity))
            case .tracks:
                TrackSelectView(app: app)
                    .transition(.move(edge: .trailing).combined(with: .opacity))
            case .race(let id):
                RaceView(app: app, trackID: id)
                    .transition(.opacity)
            case .results:
                ResultsView(app: app)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .preferredColorScheme(.light)
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .onChange(of: scenePhase) { _, phase in
            // Being suspended stops the audio graph without posting an
            // interruption, so coming back has to be handled here or the
            // game plays the rest of the session in silence.
            if phase == .active { GameAudio.shared.resume() }
        }
    }
}
