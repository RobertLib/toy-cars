//
//  Haptics.swift
//  ToyCars
//

import UIKit

enum Haptics {
    private static let light = UIImpactFeedbackGenerator(style: .light)
    private static let medium = UIImpactFeedbackGenerator(style: .medium)
    private static let heavy = UIImpactFeedbackGenerator(style: .heavy)
    private static let notify = UINotificationFeedbackGenerator()

    /// Mirrors the player's setting. The race fires a haptic on every bump,
    /// pickup and lap, which is a lot of buzzing for someone who does not
    /// want any - and turning the sound off did not use to turn this off too.
    static var enabled = true

    static func prepare() {
        guard enabled else { return }
        light.prepare(); medium.prepare(); heavy.prepare()
    }
    static func tap() {
        guard enabled else { return }
        light.impactOccurred(intensity: 0.7)
    }
    static func bump(_ i: CGFloat) {
        guard enabled else { return }
        medium.impactOccurred(intensity: max(0.2, min(1, i)))
    }
    static func crash() {
        guard enabled else { return }
        heavy.impactOccurred()
    }
    static func success() {
        guard enabled else { return }
        notify.notificationOccurred(.success)
    }
    static func warn() {
        guard enabled else { return }
        notify.notificationOccurred(.warning)
    }
}
