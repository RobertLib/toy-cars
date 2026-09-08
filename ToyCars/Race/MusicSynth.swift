//
//  MusicSynth.swift
//  ToyCars
//
//  A small sequencer that plays a cheerful loop straight on the audio thread.
//  No music files - just bass, arpeggio, hi-hat and kick.
//  The chords run I - V - vi - IV, a rewarding progression for playful music.
//

import Foundation

/// Everything runs inside the render callback, hence no allocations or locks.
///
/// `nonisolated` for the same reason as `AudioParams`, and it matters more
/// here: `render()` is called once per sample, 44,100 times a second, on the
/// audio thread. Under this project's `SWIFT_DEFAULT_ACTOR_ISOLATION =
/// MainActor` an unannotated class is main-actor isolated, so those calls
/// come with an isolation check that fails on the very first buffer.
nonisolated final class MusicSynth: @unchecked Sendable {

    // ------------------------------------------------------------ notes
    /// Semitones above C. Chords for the four bars of the loop.
    private static let chords: [[Int]] = [
        [0, 4, 7, 12],        // C major
        [7, 11, 14, 19],      // G major
        [9, 12, 16, 21],      // A minor
        [5, 9, 12, 17],       // F major
    ]
    /// Melody in sixteenths as an index into the chord, -1 = rest.
    private static let arpPattern: [Int] = [
        0, 2, 1, 3, 0, 2, 3, 1,
        0, 2, 1, 3, 2, 3, 1, 2,
    ]
    private static let bassPattern: [Int] = [
        0, -1, -1, 0, -1, 0, -1, -1,
        0, -1, -1, 0, -1, 0, -1, 0,
    ]
    private static let hatPattern: [Int] = [
        0, 0, 1, 0, 0, 0, 1, 0,
        0, 0, 1, 0, 0, 1, 1, 0,
    ]
    private static let kickPattern: [Int] = [
        1, 0, 0, 0, 0, 0, 1, 0,
        1, 0, 0, 0, 0, 0, 0, 0,
    ]

    // ------------------------------------------------------------ state
    var gain: Float = 0            // target volume 0..1
    private var gainSmooth: Float = 0
    private let sampleRate: Float
    private var samplesPerStep: Float
    private var stepAcc: Float = 0
    private var step = 0
    private var bar = 0

    private var arpPhase: Float = 0
    private var arpFreq: Float = 0
    private var arpEnv: Float = 0
    private var bassPhase: Float = 0
    private var bassFreq: Float = 0
    private var bassEnv: Float = 0
    private var hatEnv: Float = 0
    private var kickPhase: Float = 0
    private var kickEnv: Float = 0
    private var kickFreq: Float = 0
    private var rng: UInt32 = 5731
    private var lp: Float = 0

    init(sampleRate: Float = 44100, bpm: Float = 128) {
        self.sampleRate = sampleRate
        // sixteenths: four steps per beat
        self.samplesPerStep = sampleRate * 60.0 / (bpm * 4)
    }

    func setTempo(bpm: Float) {
        samplesPerStep = sampleRate * 60.0 / (bpm * 4)
    }

    @inline(__always)
    private func note(_ semitone: Int, octave: Float = 0) -> Float {
        // C4 = 261.63 Hz
        261.63 * powf(2, (Float(semitone) / 12.0) + octave)
    }

    @inline(__always)
    private func nextNoise() -> Float {
        rng = rng &* 1_664_525 &+ 1_013_904_223
        return Float(Int32(bitPattern: rng)) / Float(Int32.max)
    }

    private func trigger() {
        let chord = MusicSynth.chords[bar % MusicSynth.chords.count]
        let s = step % 16

        let ai = MusicSynth.arpPattern[s]
        if ai >= 0 {
            arpFreq = note(chord[ai % chord.count], octave: 1)
            arpEnv = 1
        }
        let bi = MusicSynth.bassPattern[s]
        if bi >= 0 {
            bassFreq = note(chord[0], octave: -2)
            bassEnv = 1
        }
        if MusicSynth.hatPattern[s] > 0 { hatEnv = 1 }
        if MusicSynth.kickPattern[s] > 0 {
            kickEnv = 1
            kickFreq = 120
        }
    }

    /// One sample of the loop.
    @inline(__always)
    func render() -> Float {
        gainSmooth += (gain - gainSmooth) * 0.00008
        if gainSmooth < 0.0004 && gain <= 0 { return 0 }

        stepAcc += 1
        if stepAcc >= samplesPerStep {
            stepAcc -= samplesPerStep
            step += 1
            if step % 16 == 0 { bar += 1 }
            trigger()
        }

        var out: Float = 0

        // arpeggio - triangle wave with a fast decay
        if arpEnv > 0.0005 {
            arpPhase += arpFreq / sampleRate
            if arpPhase >= 1 { arpPhase -= 1 }
            let tri = 4 * abs(arpPhase - 0.5) - 1
            out += tri * arpEnv * 0.16
            arpEnv *= 0.99975
        }
        // bass - soft square wave
        if bassEnv > 0.0005 {
            bassPhase += bassFreq / sampleRate
            if bassPhase >= 1 { bassPhase -= 1 }
            let sq: Float = bassPhase < 0.5 ? 1 : -1
            out += sq * bassEnv * 0.13
            bassEnv *= 0.99988
        }
        // hi-hat - filtered noise
        if hatEnv > 0.0005 {
            let nz = nextNoise()
            lp += (nz - lp) * 0.55
            out += (nz - lp) * hatEnv * 0.10
            hatEnv *= 0.9990
        }
        // kick - descending sine
        if kickEnv > 0.0005 {
            kickPhase += kickFreq / sampleRate
            if kickPhase >= 1 { kickPhase -= 1 }
            out += sinf(2 * .pi * kickPhase) * kickEnv * 0.28
            kickEnv *= 0.99975
            kickFreq = max(45, kickFreq * 0.9994)
        }

        return out * gainSmooth
    }
}
