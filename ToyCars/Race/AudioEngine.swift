//
//  AudioEngine.swift
//  ToyCars
//
//  All audio is generated at runtime - engine, skid and wind as continuous
//  tones, sound effects as short synthesised buffers. The game therefore needs
//  no audio files and the engine sound tracks the revs exactly.
//

import AVFoundation
import Foundation

/// Parameters shared with the audio thread. Reading and writing plain Floats
/// is common and safe practice for audio parameters.
///
/// `nonisolated` is load-bearing, not decoration. This project builds with
/// `SWIFT_DEFAULT_ACTOR_ISOLATION = MainActor`, so an unannotated type is
/// main-actor isolated - and the render block below reads these fields on the
/// audio thread. Left to the default, the compiler puts an isolation check in
/// front of every one of those reads, and the first buffer the audio unit
/// pulls trips `dispatch_assert_queue` and takes the app down with a SIGTRAP.
/// The whole point of this object is to be read from off the main actor.
nonisolated final class AudioParams: @unchecked Sendable {
    var engineFreq: Float = 60
    var engineGain: Float = 0
    var skidGain: Float = 0
    var windGain: Float = 0
    var rumbleGain: Float = 0
    var masterGain: Float = 1
    var musicGain: Float = 0
}

@MainActor
final class GameAudio {
    static let shared = GameAudio()

    // Not `let`: a media-services reset invalidates both, and the only
    // recovery Apple offers is to build new ones.
    private var engine = AVAudioEngine()
    private var source: AVAudioSourceNode?
    private var sfxPlayer = AVAudioPlayerNode()
    private var graphBuilt = false
    private var observing = false
    private let params = AudioParams()
    private let music = MusicSynth()
    var musicEnabled = true {
        didSet { applyMusic() }
    }
    private var musicContext: Float = 0.30
    private var format: AVAudioFormat!
    private var running = false
    var enabled = true {
        didSet {
            params.masterGain = enabled ? 1 : 0
            applyMusic()
        }
    }

    private init() {}

    private func applyMusic() {
        params.musicGain = (musicEnabled && enabled) ? musicContext : 0
    }

    /// 0 = silence, 0.30 = menu, 0.19 = race (so it does not drown the engine)
    func setMusicContext(_ level: Float) {
        musicContext = level
        applyMusic()
    }

    // ------------------------------------------------------------ start

    /// Starts the engine, building the audio graph the first time. Safe to
    /// call again after an interruption or a route change: the graph is built
    /// once and only the engine is restarted.
    func start() {
        guard !running else { return }
        activateSession()
        observeSessionEvents()
        if !graphBuilt { buildGraph() }
        do {
            try engine.start()
            sfxPlayer.play()
            running = true
            applyMusic()
        } catch {
            running = false
        }
    }

    private func activateSession() {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.ambient, mode: .default,
                                    options: [.mixWithOthers])
            try session.setActive(true)
        } catch { }
    }

    private func buildGraph() {
        let sr = 44100.0
        format = AVAudioFormat(standardFormatWithSampleRate: sr, channels: 2)
        let node = AVAudioSourceNode(
            renderBlock: GameAudio.makeRenderBlock(params: params,
                                                   music: music))
        source = node
        engine.attach(node)
        engine.attach(sfxPlayer)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        engine.connect(sfxPlayer, to: engine.mainMixerNode, format: format)
        engine.mainMixerNode.outputVolume = 0.85
        graphBuilt = true
    }

    /// The render block, built somewhere the main actor cannot reach.
    ///
    /// `nonisolated` is what makes this a separate function at all. A closure
    /// written inline in `buildGraph` inherits that method's `@MainActor`, and
    /// the compiler then guards its every entry with an isolation check - so
    /// the first buffer the audio unit pulled tripped
    /// `dispatch_assert_queue` on the audio thread and killed the app with a
    /// SIGTRAP before the menu had finished appearing. `@Sendable` is not an
    /// option either: the oscillator state below is captured mutably, which
    /// is exactly what a `@Sendable` closure may not do. Declaring the
    /// factory nonisolated leaves the block nonisolated, which is what a
    /// real-time callback has to be.
    private nonisolated static func makeRenderBlock(
        params p: AudioParams, music mus: MusicSynth
    ) -> AVAudioSourceNodeRenderBlock {
        var phase: Float = 0
        var phase2: Float = 0
        var noiseState: Float = 0
        var lp1: Float = 0
        var lp2: Float = 0
        var rng: UInt32 = 22222

        return { _, _, frameCount, audioBufferList in
            let abl = UnsafeMutableAudioBufferListPointer(audioBufferList)
            let n = Int(frameCount)
            let sampleRate: Float = 44100
            let f = p.engineFreq
            let eg = p.engineGain * p.masterGain
            let sg = p.skidGain * p.masterGain
            let wg = p.windGain * p.masterGain
            let rg = p.rumbleGain * p.masterGain
            mus.gain = p.musicGain * p.masterGain
            let inc = f / sampleRate
            let inc2 = (f * 1.5) / sampleRate

            for i in 0..<n {
                // --- engine: two saws + a soft fifth = a "buzzy" toy engine
                phase += inc
                if phase >= 1 { phase -= 1 }
                phase2 += inc2
                if phase2 >= 1 { phase2 -= 1 }
                let saw = (phase * 2 - 1)
                let saw2 = (phase2 * 2 - 1)
                let square: Float = phase < 0.42 ? 1 : -1
                var engineS = saw * 0.55 + saw2 * 0.22 + square * 0.20
                // softer sound - one-pole low-pass
                lp1 += (engineS - lp1) * 0.34
                engineS = lp1

                // --- noise for skid and wind
                rng = rng &* 1_664_525 &+ 1_013_904_223
                let white = Float(Int32(bitPattern: rng)) / Float(Int32.max)
                noiseState += (white - noiseState) * 0.55
                lp2 += (noiseState - lp2) * 0.10
                let skid = (noiseState - lp2) * 1.4      // band-pass
                let wind = lp2 * 1.6

                var s = engineS * eg * 0.34
                s += skid * sg * 0.30
                s += wind * wg * 0.18
                s += lp2 * rg * 0.5
                s += mus.render()
                s = max(-1, min(1, s))

                for buf in abl {
                    let ptr = buf.mData!.assumingMemoryBound(to: Float.self)
                    ptr[i] = s
                }
            }
            return noErr
        }
    }

    // ------------------------------------------------- session interruptions

    /// A phone call, Siri or a media-services reset stops the engine behind
    /// our back. Without these observers `running` stayed true, so `start()`
    /// returned early and the game was silent for the rest of the session.
    private func observeSessionEvents() {
        guard !observing else { return }
        observing = true
        let centre = NotificationCenter.default
        let session = AVAudioSession.sharedInstance()

        centre.addObserver(forName: AVAudioSession.interruptionNotification,
                           object: session, queue: .main) { note in
            let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt
            let type = raw.flatMap(AVAudioSession.InterruptionType.init(rawValue:))
            let options = AVAudioSession.InterruptionOptions(
                rawValue: note.userInfo?[
                    AVAudioSessionInterruptionOptionKey] as? UInt ?? 0)
            Task { @MainActor in
                GameAudio.shared.handleInterruption(type, options: options)
            }
        }

        // Headphones unplugged, a Bluetooth speaker going away: the engine can
        // stop with no interruption notification at all.
        centre.addObserver(forName: AVAudioSession.routeChangeNotification,
                           object: session, queue: .main) { _ in
            Task { @MainActor in GameAudio.shared.restartIfStopped() }
        }

        // The audio server died. Everything built on it is invalid, so the
        // graph has to be thrown away and made again from scratch.
        centre.addObserver(
            forName: AVAudioSession.mediaServicesWereResetNotification,
            object: session, queue: .main) { _ in
            Task { @MainActor in GameAudio.shared.rebuildAfterReset() }
        }

        // The graph's own configuration changed under it - a different output
        // format, a device coming or going. The engine stops, and this is the
        // notification that says so. `object: nil` rather than the engine,
        // because `rebuildAfterReset` replaces the engine and these observers
        // are only ever registered once.
        centre.addObserver(forName: .AVAudioEngineConfigurationChange,
                           object: nil, queue: .main) { _ in
            Task { @MainActor in GameAudio.shared.restartIfStopped() }
        }
    }

    private func handleInterruption(_ type: AVAudioSession.InterruptionType?,
                                    options: AVAudioSession.InterruptionOptions) {
        switch type {
        case .began:
            silenceDriving()
            running = false
        case .ended:
            guard options.contains(.shouldResume) else { return }
            start()
        default:
            break
        }
    }

    private func restartIfStopped() {
        guard graphBuilt, !engine.isRunning else { return }
        running = false
        start()
    }

    /// Coming back to the foreground.
    ///
    /// An `.ambient` session is torn down when the app is suspended, and that
    /// is not an interruption, a route change or a media reset - so none of
    /// the three observers above fired, `running` was left standing at true,
    /// and `start()` returned early for the rest of the session. The game
    /// came back from a home-screen trip silent, and walking out to the main
    /// menu could not bring it back either, because that calls `start()` too.
    /// Re-activating the session is cheap and safe when nothing was lost.
    func resume() {
        guard graphBuilt else { return }
        activateSession()
        restartIfStopped()
    }

    private func rebuildAfterReset() {
        running = false
        graphBuilt = false
        source = nil
        buffers.removeAll()
        engine = AVAudioEngine()
        sfxPlayer = AVAudioPlayerNode()
        start()
    }

    // ------------------------------------------------------------ driving

    func updateDriving(speed: Float, topSpeed: Float, throttle: Float,
                       slip: Float, offTrack: Bool, boost: Bool,
                       airborne: Bool) {
        guard enabled else {
            params.engineGain = 0; params.skidGain = 0
            params.windGain = 0; params.rumbleGain = 0
            return
        }
        let ratio = min(1.3, speed / max(4, topSpeed))
        // "gearbox" - the revs drop back down four times per lap
        let gear = min(4, Int(ratio * 4.4))
        let inGear = ratio * 4.4 - Float(gear)
        let rpm = 0.32 + inGear * 0.62 + Float(gear) * 0.045
        params.engineFreq = 52 + rpm * 168 + (boost ? 34 : 0)
        let load = 0.30 + max(0, throttle) * 0.55 + ratio * 0.2
        params.engineGain = airborne ? load * 0.55 : load
        params.skidGain = min(0.85, max(0, slip - 0.18) * 1.5)
        params.windGain = min(0.7, ratio * 0.55)
        params.rumbleGain = offTrack ? min(0.5, ratio * 0.6) : 0
    }

    func silenceDriving() {
        params.engineGain = 0
        params.skidGain = 0
        params.windGain = 0
        params.rumbleGain = 0
    }

    // ------------------------------------------------------------ effects

    private var buffers: [String: AVAudioPCMBuffer] = [:]

    private func tone(_ key: String, freqs: [Float], duration: Float,
                      decay: Float, gain: Float, noise: Float = 0,
                      sweep: Float = 0) -> AVAudioPCMBuffer? {
        if let b = buffers[key] { return b }
        guard let fmt = format else { return nil }
        let sr = Float(fmt.sampleRate)
        let n = AVAudioFrameCount(duration * sr)
        guard let buf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: n)
        else { return nil }
        buf.frameLength = n
        var rng: UInt32 = 9781
        for ch in 0..<Int(fmt.channelCount) {
            guard let data = buf.floatChannelData?[ch] else { continue }
            for i in 0..<Int(n) {
                let t = Float(i) / sr
                let env = expf(-t * decay)
                var s: Float = 0
                for (k, f) in freqs.enumerated() {
                    let ff = f * (1 + sweep * t)
                    s += sinf(2 * .pi * ff * t) / Float(k + 1)
                }
                if noise > 0 {
                    rng = rng &* 1_664_525 &+ 1_013_904_223
                    s += (Float(Int32(bitPattern: rng)) / Float(Int32.max)) * noise
                }
                data[i] = max(-1, min(1, s * env * gain))
            }
        }
        buffers[key] = buf
        return buf
    }

    private func play(_ buf: AVAudioPCMBuffer?) {
        guard enabled, running, let buf else { return }
        sfxPlayer.scheduleBuffer(buf, at: nil, options: [],
                                 completionHandler: nil)
    }

    func beep(_ n: Int) {
        let f: Float = n == 0 ? 880 : 440
        play(tone("beep\(n)", freqs: [f, f * 2], duration: n == 0 ? 0.5 : 0.22,
                  decay: n == 0 ? 4 : 9, gain: 0.34))
    }

    func pickup() {
        play(tone("pickup", freqs: [660, 990, 1320], duration: 0.26,
                  decay: 12, gain: 0.26, sweep: 0.9))
    }

    func boost() {
        play(tone("boost", freqs: [200, 300], duration: 0.5, decay: 5,
                  gain: 0.28, noise: 0.35, sweep: 2.4))
    }

    func lap() {
        play(tone("lap", freqs: [523, 659, 784], duration: 0.42, decay: 5,
                  gain: 0.24))
    }

    func crash(_ strength: Float) {
        // `tone` caches by key, so the strength has to be part of the key or
        // every crash for the rest of the session plays back at the volume of
        // the first one. Quantised to four steps: audibly graded, and at most
        // four buffers instead of one per distinct impact.
        let level = (max(0.4, min(1, strength)) * 4).rounded() / 4
        play(tone("crash\(level)", freqs: [90, 130], duration: 0.28, decay: 14,
                  gain: 0.30 * level, noise: 0.7))
    }

    func finish() {
        play(tone("finish", freqs: [523, 784, 1046], duration: 0.9, decay: 2.6,
                  gain: 0.26))
    }

    func uiTap() {
        play(tone("uitap", freqs: [720], duration: 0.09, decay: 30, gain: 0.16))
    }
}
