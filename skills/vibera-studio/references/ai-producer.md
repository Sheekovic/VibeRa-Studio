# CoProducer and local AI music

`vibera_ai` exposes CoProducer's chat (a language-model producer that edits the
project through the same canonical tools) and local music generation. CoProducer
is one docked, non-modal side panel with two tabs: Chat and MIDI ideas (the local
Magenta generator).

- `{"action":"state"}`: provider preset/baseUrl/model/effort and `hasApiKey`
  (keys are never returned), connection test result, music engine install/server
  state, the current music job (`jobState`, `stage`, `progress`, `audio`) and the
  agent (`busy`, `events`) and `panel {visible, tab, width}`.
- `configure`: `provider {preset, baseUrl, model, effort}`; effort is `""` (model
  default) or low|medium|high|xhigh|max|ultra (Anthropic uses low..max); write-only `apiKey`
  (stored DPAPI-encrypted for the current Windows user, per preset);
  `music {engineRoot, aceStepUrl, aceStepAutoStart, aceDit, aceLm, yueLowVram, yueLicenseAccepted}`;
  `aceDit` is empty (auto) or acestep-v15-turbo|sft|base|xl-turbo|xl-sft|xl-base,
  `aceLm` empty (auto) or acestep-5Hz-lm-0.6B|1.7B|4B|none. `state.music.gpu`,
  `aceRecommended` (official VRAM tier table) and `aceEffective` show the choice;
  a change restarts only a server VibeRa started (XL below 20 GB uses CPU offload);
  `maxAgentSteps`; `agentMayEditProject` (false = read-only tools only).
  Presets: codex-cli (the user's ChatGPT subscription through the signed-in local
  Codex CLI; no key, no separate API billing), claude-cli (the user's Claude Pro/Max
  subscription through the signed-in Claude Code CLI; models default|opus|sonnet|
  haiku|fable, effort low..max; no built-in tools, only VibeRa's MCP server), anthropic (API key, default
  `claude-opus-5-5`), openai (API key, billed separately), gemini, openrouter,
  deepseek, custom (cloud OpenAI-compatible), ollama, lmstudio, localcustom.
  On startup VibeRa detects `codex login status`; when the chosen provider needs a
  key that is not stored, it switches to codex-cli automatically. `state.codex`
  reports installed/loggedIn/chatgpt/version, `models[]` (slug/name/efforts/
  defaultEffort from `codex debug models`) and `effectiveEffort`; `detectAgents`
  re-checks both CLIs in the background (`state.agent.detecting`); `state.claudeCli`
  reports installed/loggedIn/subscription/version/models. A Codex model is passed as `-c model=...` and an effort as
  `-c model_reasoning_effort=...` only when that model accepts it.
  With codex-cli, VibeRa runs `codex exec --json` read-only in a VibeRa-owned folder
  and serves its tools over a 127.0.0.1 token-protected bridge
  (`VibeRa Studio.exe --mcp-bridge <port> <token>`); only VibeRa's tools are
  pre-approved, read-only mode still applies, and follow-ups resume the thread.
- `testConnection`, then poll `state.connection` for `ok` and `models`.
- `generateMusic`: `engine` ace-step (10..600 s, vocals or instrumental, MIT) or
  yue2 (lyrics-to-song; CC BY-NC 4.0 weights, set `yueLicenseAccepted` first),
  `prompt`, `lyrics` ([Verse]/[Chorus] tags), `language`, `seconds`, `bpm` (0 auto),
  `key`, `seed` (-1 random), `instrumental`, `beat`, `lane` (-1 = first free lane).
  First use installs the engine automatically (multi-GB downloads, NVIDIA GPU
  required) and starts an owned ACE-Step server. Poll `state.music`; on success
  the WAV/FLAC is imported as a new Audio Clip Channel (its own Undo step).
- `prepareEngine` installs/checks an engine without generating; `cancelMusic`;
  `stopServer` stops only the server VibeRa started.
- `chat` sends a message to the in-app producer; poll `events` with `since` for
  user/assistant/tool/error/status rows until `busy` is false; `cancelChat`,
  `resetChat`.
- `panel` shows or hides the docked CoProducer panel: `visible` (default true),
  `tab` chat|midi.

## MIDI ideas (local models)

The panel's MIDI ideas tab is driven with `idea*` actions, through its own controls and
prerequisites (a button disabled in the panel is refused with `coproducer.unavailable`):

- `ideaState`: `busy`, `candidates`, `summary`, `model`, `destination`/`destinations` (Channel IDs),
  `families`, `variations`, region (`start` quarter notes, `bars`), `seed`, `context`, `canGenerate`.
- `ideaConfigure`: `model` melody (MelodyRNN continuation) | multitrack (MusicVAE parts) |
  anticipation (experimental), `destination`, `start`, `bars` 1..64, `family`, `variation`,
  `variationAmount` 0..100, `seed`, `chord` + `pitchClasses` (0..11), `lockSelection`,
  `replaceRegion`, `context` auto | all | [Channel IDs], `installation` (absolute model folder, the
  Models... button) and `runtime` (absolute Python/Node executable, the Runtime... button); both read
  back in `ideaState`. Refused while busy.
- `ideaGenerate` (or `ideaInspect` to read context only) runs asynchronously; poll `ideaState` until
  `busy` is false. The project is unchanged until accept.
- `ideaAudition` / `ideaStopAudition` `{candidate}`, `ideaAccept {candidate}` (one Undo step:
  `coproducer.accept`, or `coproducer.replace` with `replaceRegion`), `ideaDiscard`, `ideaCancel`.
- `ideaMapping {channel, program, pitches, clear}`: explicit GM mapping for Anticipation.

Channel or project changes rebuild an idle panel with default settings, as in the UI. Generated
notes are model output, not listening evidence.

The in-app agent receives every MCP tool except `vibera_ai`, `vibera_pointer` and
`vibera_key`, plus `vibera_generate_music`. UI: the CoProducer toolbar button (or Tools >
CoProducer panel) opens the right-side panel; its composer picks model and effort;
Settings > AI chooses the provider. Generated audio quality and model musicality are not qualified.
