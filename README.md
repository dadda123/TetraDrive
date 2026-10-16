# TetraDrive

TetraDrive is a faithful emulation of a popular multiband distortion unit from the early 2000s. It splits the signal into four frequency bands and drives each one through a waveshaper, then sums them back together.

**Free download (Windows, VST3):** [daddasounds.com](https://daddasounds.com/products/tetradrive.html)

## Features

* Four bands with independent Drive and Level, five waveshaper curves, Input and Output gain.
* Oversampling (2x, 4x, 8x) around the waveshapers, with latency reported to the host.
* Two crossover modes: Classic (the original band-pass split) and Flat (Linkwitz-Riley, bands sum flat).
* Live spectrum analyzer (4096, 8192 or 16384 point FFT) of the input, the output or both, with the bands drawn on top.
* Drag crossovers and band levels directly on the spectrum, real filter curves are shown for every band.
* Metering: a saturation lamp per band (how much the shaper is changing the band), band level bars, and stereo input/output peak meters with clip indication.
* Sample-accurate automation
* Resizable interface
* Help bar at the bottom explains the controls

Formats: VST3 on Windows; VST3 and AU on macOS.

## Building from source

Everything needed is in this repository, including JUCE 9.0.4 in `external/JUCE`. No other downloads are required.

### Requirements

* **CMake 3.22 or newer.** Visual Studio ships with one; otherwise get it from [cmake.org](https://cmake.org/download/).
* **Windows:** Visual Studio 2022 or 2026 with the "Desktop development with C++" workload.
* **macOS:** Xcode 12.4 or newer (with command line tools).

### Windows

Open a terminal in the repository folder (for example "Developer PowerShell for VS") and run:

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target TetraDrive_VST3
```

With Visual Studio 2026, use `-G "Visual Studio 18 2026"` instead.

Keep the repository in a short path such as `C:\dev\TetraDrive`. Very long paths can exceed the Windows 260 character limit during the build.

The plug-in is in `build/TetraDrive_artefacts/Release/VST3/TetraDrive.vst3`. Copy this folder to `C:\Program Files\Common Files\VST3`.

### macOS

```
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target TetraDrive_VST3 TetraDrive_AU
```

From `build/TetraDrive_artefacts/Release/`, copy `VST3/TetraDrive.vst3` to `~/Library/Audio/Plug-Ins/VST3` and `AU/TetraDrive.component` to `~/Library/Audio/Plug-Ins/Components`. Unsigned builds may need to be allowed in System Settings > Privacy & Security before a DAW will load them.

### Building everything

`cmake --build build --config Release` builds all formats. The first build compiles JUCE and takes a few minutes; later builds are incremental.

## Presets

User presets are stored as `*.tdpreset` files in:

* Windows: `%APPDATA%\TetraDrive\Presets`
* macOS: `~/Library/Application Support/TetraDrive/Presets`

"Default" is built in. Solo, Bypass and Oversampling are not stored in presets.

## Parameters

| Name | Range | Default |
|---|---|---|
| Low Cut | 10 Hz - 1 kHz | 25 Hz |
| Crossover 1, 2, 3 | 20 Hz - 20 kHz | 136 Hz, 742 Hz, 4.05 kHz |
| High Cut | 1 - 22.05 kHz | 22.05 kHz |
| Low, Low Mid, High Mid, High Drive | -24 to +24 dB | 0 dB |
| Low, Low Mid, High Mid, High Level | -24 to +24 dB | 0 dB |
| Input, Output | -20 to +20 dB | 0 dB |
| Shape | Shape 1-5 | Shape 1 |
| Solo | Off, Low, Low Mid, High Mid, High | Off |
| Oversampling | Off, 2x, 4x, 8x | 2x |
| Crossover | Classic, Flat | Classic |
| Bypass | off/on | off |

Crossovers are kept in order with at least 1/12 octave between them, so automation can never flip the bands.

## Crossover modes

**Classic** splits the signal with fourth-order Butterworth band-pass filters, one per band, exactly as the original did. Neighbouring bands overlap at -3 dB, so with every band at 0 dB drive the sum has a little ripple around each crossover; part of the character.

**Flat** uses Linkwitz-Riley (LR4) filters. Bands cross at -6 dB and each band carries all-pass stages matching the crossovers it does not take part in, so the four bands add back to a flat magnitude response. The low and high cuts are applied once ahead of the split, where they shape the whole signal without disturbing the band sum. Pick Flat when you want the band drives to behave like independent, additive distortion layers.

Both modes use the same crossover frequencies, and the spectrum's band curves show whichever mode is active. Sessions saved before 1.1.0 load as Classic. The mode is stored in presets.

## Oversampling

Only the waveshapers run at the higher rate; the crossover filters stay at the host rate, so the band curves shown on the spectrum are exactly what you hear. Each band is upsampled with cascaded half-band polyphase filters, shaped, and downsampled again. Latency is 47, 55 or 58 samples for 2x, 4x and 8x and is reported to the host. Sessions saved before 1.1.0 load with oversampling off.

## Credits

* Made by Dadda.
* Version 1.1.0 (oversampling, Flat crossover, metering, output analyzer and the animated logo) was contributed by Steve Duda / Xfer Records.

## Project layout

```
CHANGELOG.md          version and patch notes
CMakeLists.txt        build definition
external/JUCE/        JUCE 9.0.4 with a small VST3 patch (see below)
patches/              that patch as a diff
resources/            About page text, Inter font, SVG icons
src/                  DSP engine, oversampling, parameter definitions, waveshaper tables
Source/               JUCE processor, presets, spectrum analyzer
Source/gui/           interface components
tests/                headless engine tests (-DTD_BUILD_TESTS=ON, no JUCE needed)
```

## Versioning

The version is read from the topmost `## [x.y.z]` heading in `CHANGELOG.md`. To release a new version, add a section at the top and rebuild:

```
## [1.0.1] - 2026-11-02
- Fixed: ...
```

The version then appears in the plug-in binary, the VST3/AU metadata and the About page, where "What's new" shows the notes.

## JUCE modification

JUCE's VST3 wrapper normally passes on only the last automation value per audio block. `external/JUCE` includes a small patch (`patches/juce-vst3-sample-accurate.patch`) that forwards every automation point with its sample position, which makes automation sample-accurate. After upgrading JUCE, re-apply it with `git apply patches/juce-vst3-sample-accurate.patch`.

## Licences

* TetraDrive is licensed under the GNU Affero General Public License v3 (AGPLv3), see [LICENSE.md](LICENSE.md). Copyright 2026 Dadda.
* JUCE (juce.com) is used under the AGPLv3.
* VST is a registered trademark of Steinberg Media Technologies GmbH. The VST3 SDK is licensed under the MIT licence.
* Inter typeface: SIL Open Font License 1.1 (`resources/fonts/OFL.txt`).
