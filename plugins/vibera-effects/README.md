# VibeRa Effects native integration

The rejected single-VST3 prototype is retired. Its source history and failed or
passing receipts remain in Git and build/windows/vibera-effects.

Each processor is now a fixed, individually insertable built-in Studio effect.
Implementation: src/effects; DSP catalogue: src/effects/dsp/EffectsCatalogue.h.
Tests: tests/effects/DspTests.cpp, tests/NativeEffectsTests.cpp and
scripts/verify-native-effects-mcp.py. No VST3 build, installation or scan is needed.
Current scope and actual qualification: docs/modules/VIBERA_EFFECTS.md.
