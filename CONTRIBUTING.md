# Contributing to VibeRa Studio

Thank you for wanting to help! VibeRa is a free studio built for everyone, and every
contribution matters: a bug report, a preset, a translation, a design idea or a line of code.

## Ways to help

- **Try it and tell us.** Use VibeRa for real music and [open an issue](../../issues) for anything
  that is broken, confusing, slow or missing. Screenshots, short recordings and steps to
  reproduce help a lot.
- **Fix or build something.** Pick an open issue or a roadmap item ([docs/ROADMAP.md](docs/ROADMAP.md)).
  For larger changes, open an issue first so we can agree on the approach.
- **Make sounds.** Original presets, instruments and samples are very welcome, as long as they
  are your own work (or truly free, such as CC0) and come with their license.
- **Improve the docs.** Clear explanations help new musicians and new developers alike.

## Building

Windows 10/11 x64, Visual Studio 2022 with the "Desktop development with C++" workload, and Python 3.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/fetch-dependencies.ps1
powershell -ExecutionPolicy Bypass -File scripts/apply-dependency-patches.ps1
cmake --preset windows
cmake --build --preset release --target VibeRa VibeRaPluginWorker
```

The app is built to `build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe`.

## Pull requests

- Keep each pull request focused on one change, and describe what it does and why.
- Follow the style of the surrounding code (C++20; `.clang-format` is included).
- Run the tests related to your change: `python scripts/fast-test.py -R "<test name regex>"`.
- Add or update a test when you change behaviour.
- Never commit code, presets, samples or artwork copied from commercial software. VibeRa is an
  independent implementation: write it yourself, or use material whose license allows it.

## License of contributions

By contributing, you agree that your work is released under the project's license,
the GNU Affero General Public License v3.0 or later.

## Be kind

Musicians and developers of every level are welcome here. Be patient, be respectful and
assume good intent. We are all here because we love making music.
