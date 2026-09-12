# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`).

When you cut a release:

1. Move items from `[Unreleased]` into a new `## [vX.Y.Z] - YYYY-MM-DD` section.
2. Commit the changelog update.
3. Push the tag: `git tag vX.Y.Z && git push origin vX.Y.Z`

CI reads the matching section and uses it as the GitHub Release notes. The tag
is also used in staged asset names (`<binary>-<tag>.bin`, `<binary>-<tag>.zip`).

## [Unreleased]

### Added

- The project adopts the [GWRG distribution model](https://github.com/slash-proc/gwrg-dist-spec).
  A tagged release now carries a `manifest.json` describing what it installs
  and where, an offline bundle holding every file that manifest names, and a
  GitHub Pages mirror a web installer can fetch across origins.
- The shared dist scripts, taken verbatim from the canonical set:
  `make_manifest.py`, `build_dist.py`, `make_bundle.py` and `stage_release.py`.
  They read every project-specific value out of the Makefile, so they stay
  byte-identical across projects and a fix lands everywhere at once.
- `print-SIDECARS` and `print-RO_BIN`, which the staging step reads to find any
  extra device file installed beside the binary. This homebrew is one file; the
  targets exist so the shared script needs no per-project variant.
- `print-COVER_FULL`, so the unscaled `src/assets/cover.png` is published beside
  the release next to the copy packed into the binary.

## [v0.0.1]

Tamagotchi P1 as a standalone GWHB homebrew (TamaLIB port from Retro-Go SD).

### Added

- Nothing.

### Changed

- Embedded P1 ROM — no separate `.b` file required on the SD card.

### Fixed

- Nothing.

### Install

Unzip the release archive onto the SD card root. It already contains:
- `/homebrews/TamagotchiP1.bin` — GWHB homebrew

- Optional coverflow override: `/covers/homebrew/TamagotchiP1.img` (JPEG ≤186×100,
  ≤10 KiB).
