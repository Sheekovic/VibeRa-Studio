<p align="center">
  <img src="assets/vibera-symbol-512.png" alt="VibeRa Studio" width="128">
</p>

<h1 align="center">VibeRa Studio</h1>

<p align="center">
  <b>A free, open-source music studio for Windows, with the pattern-first workflow producers already love.</b>
</p>

<p align="center">
  <a href="LICENSE"><img alt="License: AGPL-3.0" src="https://img.shields.io/badge/license-AGPL--3.0-blue"></a>
  <img alt="Platform: Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-0078d6">
  <img alt="Status: early preview" src="https://img.shields.io/badge/status-early%20preview-orange">
  <a href="CONTRIBUTING.md"><img alt="Contributions welcome" src="https://img.shields.io/badge/contributions-welcome-brightgreen"></a>
</p>

<p align="center"><a href="https://sheekovic.github.io/VibeRa-Studio/"><b>Website</b></a> &middot;
  <a href="../../releases"><b>Download</b></a> &middot;
  <a href="docs/ROADMAP.md"><b>Roadmap</b></a> &middot;
  <a href="CONTRIBUTING.md"><b>Contribute</b></a></p>

---

## Why VibeRa exists

Making music should not depend on what you can afford.

I built VibeRa Studio because I love making music, and I wanted everyone, wherever they live and
whatever their budget, to have a real studio on their computer. Over the past months I have put
**hundreds of hours** and **thousands of dollars** of my own time and money into it, and I am
giving it away **for free**, forever, under an open-source license.

This is my passion for music production, shared with you. Use it, enjoy it, make something
beautiful with it. And if you can, **help me make it better.** A studio built by a community of
musicians and developers will always go further than one person can alone.

## What you can do today

VibeRa follows the pattern-based workflow of popular music software: a Channel Rack, a Piano
Roll, a Playlist and a Mixer. Everything below is in the current preview.

**Compose**
- Step sequencer and Piano Roll with slides, portamento, chords, arpeggios, strumming, humanize and scale snapping
- Scale and maqam guide in the Piano Roll, including real quarter tones (Rast, Bayati, Saba, Sikah and more)
- Microtonal tuning per Channel: maqam presets and Scala (`.scl`) scales of any size, such as 24-EDO

**Arrange**
- Playlist with named, coloured tracks, mute and solo, collapsible track groups, markers and multiple arrangements
- Tempo changes and time-signature changes in the middle of a song
- Loop any time selection; jump between markers
- Performance mode: launch Patterns live from pads or a MIDI controller, with scenes

**Sound**
- VibeRa Synth: an original instrument with 77 presets, including recorded pianos, strings, brass and guitars from free (CC0) sample libraries
- A sampler with envelopes, filters, LFOs, pitch bend, time stretching and slicing
- 25 built-in effects, including pitch correction and a vocoder
- VST3 instruments and effects, each in its own protected process so a crashing plugin cannot take the studio down

**Record and mix**
- Audio recording with loop takes, comping, punch-in, several inputs at once and crash-safe recovery
- MIDI recording, MIDI import and export (notes, controllers, tempo and program changes)
- A full Mixer with routing, sends, sidechains, automation and latency compensation
- Export to WAV, FLAC, OGG and MP3, or one file per Mixer insert (stems)

**Comfort**
- Familiar keyboard shortcuts, all customizable, plus macros
- Choose your own interface font and colours, including a high-contrast theme
- Keyboard navigation with screen-reader announcements
- Missing-sample relinking when you move a project, and crash-safe audio takes

**Built-in AI assistant and automation**
- CoProducer: an optional assistant panel that can help you compose and edit
- A local MCP interface, so AI agents and scripts can drive most of the studio safely

## Getting started

### Download
Grab the latest installer or portable build from the [Releases](../../releases) page.
VibeRa runs on Windows 10 and 11 (64-bit) and needs no account and no license key.

### Build from source
You need Windows, **Visual Studio 2022** with the C++ desktop workload (it includes CMake),
and Python 3.

```powershell
git clone https://github.com/Sheekovic/VibeRa-Studio.git
cd VibeRa-Studio
powershell -ExecutionPolicy Bypass -File scripts/fetch-dependencies.ps1
powershell -ExecutionPolicy Bypass -File scripts/apply-dependency-patches.ps1
cmake --preset windows
cmake --build --preset release --target VibeRa VibeRaPluginWorker
```

The first build takes a while (JUCE and Tracktion Engine are large). Dependencies are pinned
to exact versions and verified by checksum, and none of them costs anything.

## This is an early preview

VibeRa is young. It is already a capable studio, but expect rough edges: features that are
still missing, behaviour that differs from other DAWs, and bugs. Please report what you find.
Every report makes the next version better.

## Roadmap

The plan is a complete, professional studio. The detailed plan lives in
[docs/ROADMAP.md](docs/ROADMAP.md). Next on the list:

- Warping audio clips to the song tempo
- Tempo automation drawn as a curve
- Microtonal tuning for external plugins (MPE / MTS-ESP)
- More original instruments and effects
- A free pad layout for performance mode
- Broader real-world testing: audio interfaces, MIDI controllers, multi-monitor setups and screen readers

## Contributing

**You are very welcome here**, whether you write code, make music, design, translate or just try
things out and tell us what feels wrong.

- **Musicians:** use VibeRa, and [open an issue](../../issues) when something sounds, looks or feels off. Compare it with the tools you know.
- **Developers:** C++20, JUCE and Tracktion Engine. Pick an issue, or a roadmap item, and open a pull request.
- **Designers:** help shape a clean, fast, beautiful interface.
- **Sound designers:** create original presets and instruments (your own work, under a free license).

See [CONTRIBUTING.md](CONTRIBUTING.md) to get started. Be kind, be patient and help each other.

## License

VibeRa Studio is free software under the **GNU Affero General Public License v3.0 or later**
(see [LICENSE](LICENSE)). You may use, study, share and improve it. Libraries keep their own
licenses: JUCE (AGPL-3.0), Tracktion Engine (GPL-3.0) and the CC0 sample collections. See
[licenses/](licenses/) and [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).

VibeRa Studio is an independent project. It is not affiliated with, endorsed by or connected to
Image-Line or any other company, and it does not read or write other applications' project
files. All product names are trademarks of their respective owners.

---

<p align="center">Made with love for music, and given to everyone for free.</p>
