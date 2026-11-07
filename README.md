# Stem Splitter

A minimal, professional audio stem separator for macOS. It runs as a **standalone app**, a **VST3** and an
**Audio Unit**. Drop in a WAV, split it into vocals, drums, bass, guitar, piano and other, then export each
stem as its own WAV, or a single WAV with any instrument or frequency range removed.

![Stem Splitter](docs/screenshot.png)

- **State-of-the-art separation**: Meta's Demucs v4 hybrid transformer neural network, run natively in C++
  ([demucs.cpp](https://github.com/sevagh/demucs.cpp)) on all CPU cores, with Apple Accelerate on macOS.
  Everything runs on your Mac; no cloud.
- **Drag and drop**: drop a WAV (AIFF, FLAC and MP3 work too) anywhere on the window.
- **Four engines**
  | Engine | Stems | Notes |
  |---|---|---|
  | 4 Stems | vocals, drums, bass, other | `htdemucs`, the default |
  | 6 Stems | + guitar, piano | `htdemucs_6s` |
  | 4 Stems HQ | vocals, drums, bass, other | `htdemucs_ft`: four specialist networks, best quality, ~4x slower |
  | Lite | vocals, drums, bass, other | instant DSP separation (no model), lower quality |
- **Audition before exporting**: play, loop, seek, mute, solo and set the level of each stem.
- **Export**
  - **Stems**: one WAV per selected stem (`Song - Vocals.wav`, ...) in a new folder.
  - **Custom mix**: one WAV of exactly what you hear. Click instruments to remove them (presets for
    *Instrumental*, *A cappella*, *No drums*, *No bass*) and/or **remove frequencies** with a low cut, high cut or
    band cut (48 dB/oct Linkwitz-Riley, > 50 dB deep, adjustable depth). The preview uses the identical filter.
  - 16-bit (dithered), 24-bit or 32-bit float; original rate or 44.1 / 48 / 96 kHz.
  - Drag a stem's handle straight into Finder or your DAW.
- Sample-rate aware: any input rate is converted with a high-quality windowed-sinc resampler and the stems
  come back at the original rate and length, sample-aligned with the source.

## Install (macOS)

Download `Stem-Splitter-macOS.dmg` from the latest [GitHub Actions build](../../actions) (or a release) and
follow `Read Me.txt` inside it. In short: drag the app to *Applications*, and copy the plug-ins to
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`. The builds aren't notarised by Apple,
so on first launch right-click the app and choose **Open**, or run
`xattr -dr com.apple.quarantine "/Applications/Stem Splitter.app"`.

The DMG is a universal binary (Apple silicon + Intel) built for macOS 11 and later, so it runs on
**macOS 14 Sonoma**.

## Using it

1. Drop a WAV file on the window.
2. Choose an engine at the top. If its model isn't installed yet, a one-click download is offered
   (or use *I have the file...* to install `.bin` files you downloaded yourself, or simply drop them on the window).
3. Press **Separate**. Progress and time remaining are shown; *Cancel* (or Esc) stops it.
4. Listen and tweak, then **Export** from the right-hand panel.

Keyboard: <kbd>Space</kbd> play/pause · <kbd>Return</kbd> back to start · <kbd>L</kbd> loop ·
<kbd>←</kbd>/<kbd>→</kbd> skip 5 s · <kbd>⌘O</kbd> open · <kbd>⌘E</kbd> export · <kbd>Esc</kbd> cancel.

In a DAW the plug-in works as a self-contained separator and player: it plays the loaded song (with your mutes,
solos and frequency cut) through its track, and stems can be dragged from it onto other tracks.

## Build from source

Requirements: CMake ≥ 3.22, Xcode command-line tools (macOS) or GCC/Clang (Linux). JUCE 8 and demucs.cpp are
fetched automatically.

```bash
# Optional: fetch the 4- and 6-stem models so they get bundled into the app
scripts/download_models.sh models 4s 6s

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DSTEMSPLITTER_MODEL_DIR="$PWD/models"
cmake --build build
ctest --test-dir build --output-on-failure
```

Outputs are in `build/StemSplitter_artefacts/Release/` (`Standalone/Stem Splitter.app`, `VST3/`, `AU/`).

CMake options:

| Option | Default | |
|---|---|---|
| `STEMSPLITTER_MODEL_DIR` | *(empty)* | Folder of `ggml-model-*.bin` files to bundle inside the app |
| `STEMSPLITTER_USE_ACCELERATE` | `ON` | Use Apple Accelerate for the network's matrix maths |
| `STEMSPLITTER_NATIVE_ARCH` | `OFF` | Optimise for the build machine's CPU only |
| `STEMSPLITTER_BUILD_PLUGIN` / `_CLI` / `_TESTS` | `ON` | Choose what to build |
| `STEMSPLITTER_MODEL_BASE_URL` | Hugging Face | Where the in-app downloader fetches models |

### Where models are looked for

1. `$STEMSPLITTER_MODEL_DIR`
2. Inside the app/plug-in bundle: `Contents/Resources/Models`
3. `~/Library/Application Support/Stem Splitter/Models` (in-app downloads go here)
4. `/Library/Application Support/Stem Splitter/Models`
5. `/Applications/Stem Splitter.app/Contents/Resources/Models` (lets the plug-ins share the app's models)

### Command-line tool

`stemsplit` uses the same engine, which is handy for batch jobs:

```bash
stemsplit song.wav                                   # 4 stems next to the song
stemsplit --engine 6 --out stems/ song.wav           # 6 stems into ./stems
stemsplit --remove vocals song.wav                   # "song (No Vocals).wav"
stemsplit --remove vocals,drums --cut low:80 song.wav
stemsplit --cut band:250-4000 --bits 16 song.wav     # frequency removal only, no separation
stemsplit --list-models                              # where models are looked for
```

## Project layout

```
Source/Core/        engine: audio I/O + resampling, Demucs and Lite separators, frequency cut,
                    mix rendering/export, model manager/downloader, background separation job
Source/UI/          look and feel, header, drop zone, stem lanes, export panel, response-curve editor
Source/PluginProcessor.*   song state, stem mixer and real-time preview playback (with resampling)
Source/PluginEditor.*      the window: drag & drop, separation and export flows, drag-out to DAW
Source/StandaloneApp.cpp   native-window standalone app (output-only audio)
Tools/stemsplit.cpp        command-line tool and DSP self tests
Tests/                     processor/playback/UI tests (run with ctest)
cmake/Demucs.cmake         builds demucs.cpp + Eigen as a static library
```

## Credits and licences

- Separation models: [Demucs](https://github.com/facebookresearch/demucs) by Meta AI (MIT).
- C++ inference: [demucs.cpp](https://github.com/sevagh/demucs.cpp) by Sevag Hanssian (MIT), using
  [Eigen](https://eigen.tuxfamily.org) (MPL2).
- Framework: [JUCE](https://juce.com) 8. JUCE is dual-licensed (AGPLv3 / commercial). Distributing closed-source
  builds requires a JUCE licence.
