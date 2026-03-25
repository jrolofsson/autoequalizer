# AutoEqualizer

AutoEqualizer is a modern C++20 command-line application for offline audio analysis and conservative correction of difficult vocal stems, mixed audio, and source-baked material that benefits from safety-biased cleanup instead of heavy-handed restoration.

The current version is deterministic, explainable, and more memory-conscious than the original MVP. It analyzes each file first, builds a per-segment plan, applies only the amount of correction the material can safely tolerate, and always writes a JSON report that explains what it decided.

## What It Does

- Decodes and encodes `WAV`, `FLAC`, `AIFF`, and `AIF`
- Analyzes files in overlapping windows and builds file-level and segment-level profiles
- Detects harshness, brittleness, sibilance, flatness, transient behavior, and high-frequency imbalance
- Protects legitimate bright and high-register vocal content from blanket de-essing or top-end cuts
- Uses a bounded YIN-style pitch estimate with smoothing to improve high-register and breathy-vocal protection
- Applies loudness-aware mastering with integrated LUFS and true-peak targets
- Supports full corrective processing or `--loudness-only` level management
- Writes per-file JSON reports for both `analyze` and `process`
- Writes a spectrogram comparison SVG for processed files so you can inspect input, output, and delta visually
- Supports single-file and folder-based batch workflows

## Build

### Dependencies

- CMake 3.30+
- A C++20 compiler
- `libsndfile`

### macOS

```bash
brew install cmake libsndfile
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### Ubuntu / Debian

```bash
sudo apt-get install cmake g++ libsndfile1-dev
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Quick Start

Analyze a file and write a report:

```bash
./build/autoequalizer analyze input.wav
```

Process a file with the default balanced mode:

```bash
./build/autoequalizer process input.wav --output out/
```

Process a whole folder and keep the directory structure:

```bash
./build/autoequalizer process ./stems --output ./cleaned --suffix _autoequalizer
```

Reports are always written:

- `analyze` writes a report next to the input inside a `reports/` folder unless you pass `--report`
- `process` writes a report under `<output>/reports/` unless you pass `--report`
- `process` also writes a sidecar spectrogram comparison SVG next to the JSON report

For single-file runs:

- `--output` can be a directory or an exact output audio filename
- `--report` can be a directory or an exact `.json` filename

For folder runs, `--output` and `--report` are treated as root directories and subfolders are preserved.

## Recommended Ways To Run It

### 1. Vocal Stem That Needs Correction

This is the main use case: adaptive correction plus stem-safe loudness handling.

```bash
./build/autoequalizer process vocal_stem.wav --output out/ --mode normal --loudness-profile stem
```

Use `artifact-safe` instead of `normal` when the source already has metallic or source-baked artifacts and stronger cleanup starts sounding dull or damaged.

### 2. Instrumental That Should Keep Its Tone

If you want level management only and do not want AutoEqualizer to apply corrective EQ, de-essing, or compression, use `--loudness-only`.

```bash
./build/autoequalizer process instrumental.wav --output out/ --loudness-only --loudness-profile stem
```

This still applies loudness normalization behavior and true-peak limiting, but skips corrective DSP.

### 3. Stem + Instrumental Workflow

If you are preparing files that will be remixed together later, use a stem-safe loudness profile for both files.

```bash
./build/autoequalizer process vocal_stem.wav --output out/ --mode normal --loudness-profile stem
./build/autoequalizer process instrumental.wav --output out/ --loudness-only --loudness-profile stem
```

That setup lets the stem receive correction while the instrumental only gets loudness management.

### 4. Finished Song Or Near-Final Mix

For material intended to behave more like a release master, use a streaming-oriented target.

```bash
./build/autoequalizer process mix.wav --output out/ --mode normal --loudness-profile streaming
./build/autoequalizer process mix.wav --output out/ --mode normal --loudness-profile spotify
./build/autoequalizer process mix.wav --output out/ --mode normal --loudness-profile apple
```

### 5. Explicit Loudness Targets

Custom targets override the selected loudness profile.

```bash
./build/autoequalizer process input.wav --output out/ --target-lufs -18 --true-peak-limit -3
./build/autoequalizer process instrumental.wav --output out/ --loudness-only --target-lufs -18 --true-peak-limit -3
```

### 6. Fragile Or Source-Baked Material

Use `artifact-safe` when the source already sounds brittle, metallic, smeared, or partially overprocessed.

```bash
./build/autoequalizer process skyline.wav --output out/ --mode artifact-safe
```

### 7. Known Problem Window

Use targeted overrides when most of the file should stay adaptive, but one timestamp range needs special handling.

```bash
./build/autoequalizer process skyline.wav --output out/ --mode artifact-safe --override-range 2:15-2:30@preserve
./build/autoequalizer process skyline.wav --output out/ --mode normal --override-range 135-150@artifact-safe
./build/autoequalizer process skyline.wav --output out/ --mode normal --override-range 0:45-0:48@bypass
```

Override syntax is:

```text
--override-range <start>-<end>@artifact-safe|preserve|bypass
```

Timestamps can be plain seconds, `mm:ss`, or `hh:mm:ss`.

## CLI Reference

```bash
./build/autoequalizer analyze <input> [--report path] [--mode preserve|artifact-safe|normal|aggressive] \
                 [--loudness-only] \
                 [--loudness-profile stem|streaming|spotify|apple|custom] \
                 [--override-range <start>-<end>@artifact-safe|preserve|bypass] \
                 [--target-lufs value] [--true-peak-limit value]

./build/autoequalizer process <input> --output path [--report path] [--suffix _autoequalizer] \
                 [--mode preserve|artifact-safe|normal|aggressive] \
                 [--loudness-only] \
                 [--loudness-profile stem|streaming|spotify|apple|custom] \
                 [--override-range <start>-<end>@artifact-safe|preserve|bypass] \
                 [--target-lufs value] [--true-peak-limit value]
```

## Processing Modes

The app is content-driven in every mode. Modes only change how much correction the engine is allowed to apply.

- `preserve`: minimal correction, strongest tone-preservation bias
- `artifact-safe`: reduced blend and safer cleanup for brittle or source-baked material
- `normal`: default balanced mode
- `aggressive`: higher correction caps for robust material

## Loudness Profiles

- `stem`: `-18 LUFS`, `-3 dBTP`, no integrated-loudness normalization pass; intended for stems and remix headroom
- `streaming`: `-14 LUFS`, `-1 dBTP`
- `spotify`: `-14 LUFS`, `-1 dBTP`
- `apple`: `-16 LUFS`, `-1 dBTP`
- `custom`: selected automatically when you provide `--target-lufs` and/or `--true-peak-limit`

The Apple preset is documented as a compatibility-oriented engineering target, not a direct Apple quote.

## How The Engine Works

AutoEqualizer does not run one static preset chain. Instead it:

1. Analyzes the file in short overlapping windows.
2. Builds file-level baselines for loudness, brightness, flatness, dynamics, and fragility.
3. Estimates pitch and voicedness to distinguish legitimate high-register content from harshness that should be corrected.
4. Scores each segment for harshness, sibilance, fragility, and correction risk.
5. Converts those scores into a per-segment plan with guardrails.
6. Smooths neighboring decisions so processing does not zipper from frame to frame.
7. Applies only the safe amount of EQ, de-essing, smoothing, compression, and output mastering for that material.

`--loudness-only` keeps the loudness and true-peak stage but zeros out the corrective DSP plan.

## Reports

Every run produces a JSON report. The sample schema lives at [docs/report.schema.json](docs/report.schema.json).

Reports include:

- file metadata
- before and after profile summaries
- hotspot lists
- segment decisions and guardrails
- loudness target information
- warnings and processing status

For long files, reports can become fairly large because the tool records segment-level decisions for explainability.

Processed runs also write a `*_spectrogram.svg` sidecar next to the JSON report. The SVG contains:

- input spectrogram
- output spectrogram
- delta panel showing output minus input energy

## Architecture Summary

- `audio/`: audio buffers, decode/encode, file discovery, chunked I/O
- `analysis/`: frame-wise feature extraction, loudness metering, profile building, hotspot detection, pitch estimation
- `dsp/`: filters, de-esser, compressor, limiter, and reusable DSP primitives
- `pipeline/`: adaptive planning, segment smoothing, guardrails, mastering, and orchestration
- `report/`: report model and streamed JSON serialization
- `cli/`: argument parsing
- `tests/`: custom test harness for analysis, DSP, planning, pipeline, and CLI behavior

## Dependency Rationale

- `libsndfile`: deterministic format support without adding a custom codec stack to the app
- Standard library elsewhere: analysis, DSP orchestration, CLI parsing, and reporting remain easy to audit

## Performance Notes

- The current pipeline avoids several large whole-file intermediate copies by using chunked audio I/O and more in-place processing.
- Batch runs process one file at a time.
- Analysis and reporting are still offline and whole-file oriented, so very long files can still produce large reports and non-trivial memory use.

## Current Limits

- The pitch tracker is stronger than the original heuristic and now uses a bounded YIN-style estimate with smoothing, but it is still not a studio-grade editor-style pitch extraction system.
- Processing remains optimized for conservative, explainable correction rather than deep restoration or perfect artifact repair.
- The loudness meter is a deterministic BS.1770-style implementation for this app, not a third-party certified metering library.
