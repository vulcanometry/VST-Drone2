# Drone 2

A layered drone synthesizer for Windows. It builds two ways from the same code:

- **Drone2.exe**: a standalone app. Audio goes to the default output device through
  WASAPI, MIDI comes in through WinMM. The last session is restored on start.
- **Drone2.vst3**: a VST3 instrument for a DAW.

Both use the same editor: a custom-drawn Win32 + GDI+ window, with no GUI framework.

## Build

Open `Drone2.sln` in Visual Studio 2022 (the **Desktop development with C++** workload
is all it needs), then build `Release | x64`. Command line:

```
msbuild Drone2.sln /p:Configuration=Release /p:Platform=x64
```

| Project | Output | Contents |
|---|---|---|
| `Drone2Core` | static lib | Engine, DSP, patches, recorder and the editor |
| `Drone2` | `bin\x64\Release\Drone2.exe` | Standalone app (`Source\Standalone`) |
| `VST3SDK` | static lib | The parts of the Steinberg VST3 SDK that a plug-in needs |
| `Drone2VST` | `bin\x64\Release\Drone2.vst3\` | VST3 plug-in bundle (`Source\Plugin`) |
| `Drone2Render` | `bin\x64\Release\Drone2Render.exe` | Offline test tool (`Source\Tools`) |

The VST3 SDK (3.8.1, MIT licensed) lives in `external\vst3sdk`. It is a git submodule
pointing at `github.com/steinbergmedia/vst3sdk`, pinned to `v3.8.1_build_84`. Only its
`base`, `pluginterfaces`, `public.sdk` and `cmake` submodules are needed, so don't use
`--recursive` (that also pulls `vstgui4`, `doc` and `tutorials`). After cloning this repo:

```
git submodule update --init
git -C external/vst3sdk submodule update --init base pluginterfaces public.sdk cmake
```

### Installing the plug-in

Copy the whole `bin\x64\Release\Drone2.vst3` folder to `C:\Program Files\Common Files\VST3\`,
then rescan plug-ins in your DAW. It shows up as **Drone 2** (Instrument|Synth). The plug-in
uses the dynamic Visual C++ runtime, so a machine without Visual Studio needs the
VC++ 2015-2022 redistributable.

## Signal flow

```
Layer A ─┐
Layer B ─┤  vector mix        envelope                                                          limiter
Layer C ─┼─ (X/Y + orbit) ──> (attack/release) ─> Filter ─> Ensemble ─> Phaser ─> Delay ─> Reverb ─> Master ─> out
Layer D ─┘

LFO 1-4, envelope, orbit, wander, mod wheel, velocity, key ──> 8-slot mod matrix ──> almost everything
```

With **HOLD** on (the default) the drone sounds without any key held, as soon as the
audio is on. MIDI notes then glide the whole drone to a new pitch, and middle C leaves it
at ROOT. With HOLD off it plays only while a key is held, with the effect tails ringing on
after release. The instrument is deliberately monophonic in pitch: chords come from the
per-layer intervals.

## The twelve algorithms

Each of the four layers runs any one of these. **MORPH**, **TIMBRE** and **COLOR** change
meaning with the algorithm, and their captions change to match.

| Algorithm | MORPH | TIMBRE | COLOR | Technique |
|---|---|---|---|---|
| **Wave Crossfade** | X-Fade | Wave A | Wave B | Phase-coherent crossfade between two points in a series of 8 waves: sine, triangle, saw, square, pulse, thin pulse, hollow, organ. Wave A and B themselves crossfade as they move |
| **Wavetable** | Position | Warp | Sub | Scans any of 20 tables, crossfading adjacent frames. Warp is a smooth phase bend; Sub is a sine an octave down |
| **Swarm** | Shape | Bloom | Octaves | Detuned stack (the classic supersaw idea). Each voice swells independently |
| **Additive** | Bright | Odd/Even | Stretch | 24 partials per voice, each slowly wandering in level; Stretch makes the series inharmonic |
| **FM** | Index | Ratio | Feedback | Two-operator FM. The index is limited by Carson's rule, so sidebands stay below Nyquist |
| **Wavefold** | Fold | Symmetry | Body | West-coast sine folding, with the gain limited by pitch |
| **Formant Choir** | Vowel | Shift | Breath | Saw through three formant filters, morphing a-e-i-o-u |
| **PAD Spectrum** | Smear | Octave | Tone | PADsynth: long loops with each harmonic smeared into a Gaussian band with random phases |
| **Bowed String** | Bow | Damping | Pressure | Karplus-Strong loop driven by continuous noise, so the string is bowed and never decays |
| **Resonator Bank** | Focus | Spacing | Noise | Noise through 8 tuned band-pass filters: airy, wind-like harmonics |
| **Grain Cloud** | Density | Scatter | Length | Up to 24 overlapping wavetable grains, scattered across consonant intervals |
| **Chaos Pair** | Cross | Ratio | Drive | Two sines phase-modulating each other (the Lyra-8 idea), with the coupling pushed by a Lorenz attractor |

### Why it stays clean

- **Wavetables** are FFT-band-limited mipmaps, one per octave and blended across the
  octave, so there are no pitch steps. A high saw has its off-harmonic energy about
  64 dB down.
- **The effects tank** is an 8-line feedback delay network: a Householder matrix for
  density, delay lengths with no common factors, and a Hadamard diffuser in front to
  remove early spatter. Its input is scaled with the decay, so a sustained drone keeps
  about the same level whether the tail lasts 1 second or 2 minutes.

## Controls

| Section | Controls |
|---|---|
| **Layer A-D** (17 each) | On, Algorithm, Table, Morph, Scan (auto-sweep depth), Scan Rate, Timbre, Color, Octave, Semi, Fine, Level, Pan, Voices (1-8), Detune, Width, Drift |
| **Vector Mix** | X/Y pad crossfading the four layers (equal power), Orbit depth, Rate (down to about 8 minutes per cycle), Path (Circle, Figure 8, Drift). The ring is where you set it; the dot is where the orbit has taken it |
| **Pitch & Amp** | Root, Tune, Glide, Attack and Release (up to 60 s), Tuning (Equal, Just, Pythagorean, Septimal, Meantone), Hold, MIDI input |
| **Filter** | Off, Low 12, Low 24, Band, High, Notch, Ladder; Cutoff, Resonance, Drive, Key tracking |
| **Effects** | Ensemble (3), Phaser (4), Tape Delay (6, the time glides like tape), FDN Reverb (8, with octave-up Shimmer and Freeze), Master (Volume, Width, Low Cut, Tilt, with a built-in limiter) |
| **Modulation** | 4 LFOs (sine, triangle, saw up/down, square, stepped, wander, Lorenz; down to about 8 minutes per cycle), 8 matrix slots (source, destination, bipolar amount) |

Voices above a limit are ignored where they would cost too much: Additive and Chaos use
up to 3, Formant and Bowed String 4, Resonator 2. Grain Cloud has its own grains, so VOICES
does nothing there, and DETUNE and DRIFT scatter the grains' pitch instead.

## Using the editor

- Drag a knob up or down. Hold **Shift** for fine control. The mouse wheel also works.
- **Double-click** a knob to reset it, or the vector pad to centre it.
- The line under the header names whatever is under the mouse.
- **Space** switches the audio. **← / →** step through patches.
- The bottom letter row plays like a piano (Z = C, S = C#, X = D ... M = B, then `,` `L` `.`),
  **-** and **=** shift the octave, and **Backspace** returns the pitch to ROOT.

### Patches

Patches are small text files (`.d2patch`) in `%APPDATA%\Drone 2\Patches`, and the patch's
name is its file name.

| Button | What it does |
|---|---|
| **Name box** | Lists the 25 factory patches by category, followed by yours |
| **‹ ›** | Previous / next patch |
| **SAVE** | Saves over the current patch. For a factory or unsaved patch it asks for a name first |
| **SAVE AS** | Saves under a new name, and asks before replacing an existing patch |
| **RENAME / DELETE** | For your own patches. Delete asks first |
| **LOAD** | Opens a patch file from anywhere on disk |
| **RANDOM** | Generates a musically constrained drone: consonant intervals, slow movement |
| **INIT** | Back to the starting sound |

A `*` after the name means the sound has been edited. A DAW project stores the full state
and the patch name itself, so it doesn't depend on your patch files.

### Recording

**REC** records the output to a 24-bit stereo WAV, named after the patch and the time, in
`Documents\Drone 2 Recordings`, and switches the audio on if needed. Press it again to
stop. **AS** does the same but asks for a file name first. **…** opens the folder.

Recording runs on its own thread, so it carries on if a plug-in's editor is closed. Very
long drones are split into `_part2`, `_part3` and so on just before a WAV file would reach
its 4 GB limit (about 4 hours per file at 48 kHz).

### Themes

**THEME** switches between Midnight, Aurora, Ember, Synthwave, Moss, Graphite and Daylight.
The choice is saved in `%APPDATA%\Drone 2\Settings.txt` and shared by the app and the plug-in.

## How the plug-in differs from the app

- **Knobs** become host parameters (125, plus Power), so they can be automated. The host
  shows the same value text as the editor.
- **POWER** is a parameter that fades the plug-in's output. The plug-in starts with it on,
  so a new instance starts droning right away (fading in over ATTACK) unless you turn HOLD off.
- **MIDI** comes from the host's track. The mod wheel reaches the **Mod Wheel** modulation
  source. Computer-keyboard notes work when the editor has focus, which depends on the host.

## Testing

```
Drone2Render check [wavDir]     every algorithm, knob extreme, table and patch: levels, DC, stability, aliasing, CPU
Drone2Render patches <dir>      patch file round trip, name validation, recorder and WAV output
Drone2Render shots <dir>        editor screenshots in every theme
```

Each check exits with a non-zero code on failure. The worst case in `check`, four layers
of 8 wavetable voices with every effect on, used about 4% of one core at 48 kHz in testing.
