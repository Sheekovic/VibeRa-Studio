// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MusicEngines.h"
#include "WorkerProcess.h"

namespace vibera::ai
{
namespace
{
constexpr const char *aceStepPortableUrl = "https://files.acemusic.ai/acemusic/win/ACE-Step-1.5.7z";
constexpr const char *uvUrl = "https://github.com/astral-sh/uv/releases/latest/download/uv-x86_64-pc-windows-msvc.zip";
constexpr const char *yueRepoZip = "https://github.com/multimodal-art-projection/YuE/archive/refs/heads/main.zip";
constexpr juce::int64 gib = juce::int64(1) << 30;

juce::var object()
{
    return juce::var(new juce::DynamicObject);
}
void set(juce::var &o, const char *key, const juce::var &value)
{
    o.getDynamicObject()->setProperty(key, value);
}
juce::String apiBase(const MusicEngineConfig &config)
{
    return config.aceStepUrl.trimCharactersAtEnd("/");
}
// Windows' own bsdtar (extracts 7z/zip). A Git/MSYS GNU tar earlier on PATH cannot read 7z and
// treats "I:\..." as a remote host, so never rely on PATH lookup; archives are passed relative to cwd.
juce::String systemTar()
{
#if JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::windowsSystemDirectory).getChildFile("tar.exe").getFullPathName();
#else
    return "/usr/bin/tar";
#endif
}
juce::File findFile(const juce::File &root, const juce::String &name, int depth = 3)
{
    if (root.getChildFile(name).existsAsFile())
        return root.getChildFile(name);
    if (depth == 0)
        return {};
    for (const auto &child : root.findChildFiles(juce::File::findDirectories, false))
        if (auto found = findFile(child, name, depth - 1); found != juce::File{})
            return found;
    return {};
}
} // namespace

juce::Result validateMusicRequest(const MusicRequest &r)
{
    if (r.engine != "ace-step" && r.engine != "yue2")
        return juce::Result::fail("music.invalidEngine: engine must be ace-step or yue2");
    if (r.prompt.trim().isEmpty())
        return juce::Result::fail("music.invalidPrompt: A style/genre prompt is required");
    if (r.prompt.length() > 2000 || r.lyrics.length() > 10000)
        return juce::Result::fail("music.tooLong: Prompt up to 2000 and lyrics up to 10000 characters");
    if (!std::isfinite(r.seconds) || r.seconds < 10 || r.seconds > 600)
        return juce::Result::fail("music.invalidDuration: seconds must be 10..600");
    if (!std::isfinite(r.bpm) || (r.bpm != 0 && (r.bpm < 30 || r.bpm > 300)))
        return juce::Result::fail("music.invalidBpm: bpm must be 0 (auto) or 30..300");
    return juce::Result::ok();
}

juce::var musicRequestJson(const MusicRequest &r)
{
    auto o = object();
    set(o, "engine", r.engine);
    set(o, "prompt", r.prompt);
    set(o, "lyrics", r.lyrics);
    set(o, "language", r.language);
    set(o, "seconds", r.seconds);
    set(o, "bpm", r.bpm);
    set(o, "key", r.key);
    set(o, "seed", r.seed);
    set(o, "instrumental", r.instrumental);
    return o;
}

MusicRequest musicRequestFromJson(const juce::var &json)
{
    MusicRequest r;
    r.engine = json.getProperty("engine", "ace-step").toString();
    r.prompt = json["prompt"].toString();
    r.lyrics = json["lyrics"].toString();
    r.language = json.getProperty("language", "en").toString();
    r.seconds = double(json.getProperty("seconds", 60));
    r.bpm = double(json.getProperty("bpm", 0));
    r.key = json["key"].toString();
    r.seed = int(json.getProperty("seed", -1));
    r.instrumental = bool(json.getProperty("instrumental", false));
    return r;
}

juce::var aceStepTaskBody(const MusicRequest &r)
{
    auto body = object();
    set(body, "prompt", r.prompt);
    set(body, "lyrics", r.instrumental || r.lyrics.trim().isEmpty() ? juce::String("[Instrumental]") : r.lyrics);
    set(body, "vocal_language", r.language);
    set(body, "audio_duration", r.seconds);
    if (r.bpm > 0)
        set(body, "bpm", int(std::lround(r.bpm)));
    if (r.key.isNotEmpty())
        set(body, "key_scale", r.key);
    set(body, "seed", r.seed);
    set(body, "use_random_seed", r.seed < 0);
    set(body, "batch_size", 1);
    set(body, "task_type", "text2music");
    set(body, "audio_format", "wav");
    return body;
}

juce::var yueRequestBody(const MusicRequest &r)
{
    auto body = object();
    juce::String style = r.prompt;
    if (r.bpm > 0)
        style << ", " << int(std::lround(r.bpm)) << " BPM";
    if (r.key.isNotEmpty())
        style << ", " << r.key;
    set(body, "style", style);
    set(body, "lyrics", r.instrumental ? juce::String{} : r.lyrics);
    set(body, "cot", "full");
    if (r.seed >= 0)
        set(body, "seed", r.seed);
    return body;
}

juce::StringArray aceStepAudioReferences(const juce::var &value)
{
    juce::StringArray found;
    std::function<void(const juce::var &)> visit = [&](const juce::var &v)
    {
        if (v.isString())
        {
            const auto text = v.toString();
            if (text.contains("/v1/audio?path="))
                found.addIfNotAlreadyThere(text.fromFirstOccurrenceOf("/v1/audio", true, false)
                                               .upToFirstOccurrenceOf("\"", false, false));
            else if (text.startsWithChar('[') || text.startsWithChar('{'))
                visit(juce::JSON::parse(text)); // query_result may carry a JSON-encoded result string.
        }
        else if (const auto *array = v.getArray())
            for (const auto &item : *array)
                visit(item);
        else if (const auto *dynamic = v.getDynamicObject())
            for (const auto &property : dynamic->getProperties())
                visit(property.value);
    };
    visit(value);
    return found;
}

int aceStepTaskStatus(const juce::var &queryResult, const juce::String &taskId)
{
    const auto data = queryResult.hasProperty("data") ? queryResult["data"] : queryResult;
    if (const auto *rows = data.getArray())
        for (const auto &row : *rows)
            if (row["task_id"].toString() == taskId || rows->size() == 1)
                return row.hasProperty("status") ? int(row["status"]) : -1;
    return -1;
}

GpuInfo detectGpu()
{
    GpuInfo info;
    juce::ChildProcess smi;
    if (!smi.start(juce::StringArray{"nvidia-smi", "--query-gpu=name,memory.total", "--format=csv,noheader,nounits"}))
        return info;
    const auto output = smi.readAllProcessOutput().trim();
    if (smi.getExitCode() != 0 || output.isEmpty())
        return info;
    const auto first = output.upToFirstOccurrenceOf("\n", false, false);
    info.nvidia = true;
    info.name = first.upToLastOccurrenceOf(",", false, false).trim();
    info.vramMb = first.fromLastOccurrenceOf(",", false, false).trim().getIntValue();
    return info;
}

const GpuInfo &cachedGpu()
{
    static const GpuInfo info = detectGpu();
    return info;
}

const juce::StringArray &aceStepDitModels()
{
    static const juce::StringArray models{"acestep-v15-turbo", "acestep-v15-sft", "acestep-v15-base",
                                          "acestep-v15-xl-turbo", "acestep-v15-xl-sft", "acestep-v15-xl-base"};
    return models;
}

const juce::StringArray &aceStepLmModels()
{
    static const juce::StringArray models{"acestep-5Hz-lm-0.6B", "acestep-5Hz-lm-1.7B", "acestep-5Hz-lm-4B", "none"};
    return models;
}

AceStepModels recommendAceStep(int vramMb)
{
    const double gb = vramMb / 1024.0;
    if (vramMb <= 0)
        return {"acestep-v15-turbo", "acestep-5Hz-lm-0.6B", true, true, "GPU memory unknown: safe default"};
    if (gb < 6)
        return {"acestep-v15-turbo", "none", true, true, "Under 6 GB: turbo without the language model, offloaded"};
    if (gb < 12)
        return {"acestep-v15-turbo", "acestep-5Hz-lm-0.6B", true, true, "6-12 GB: turbo + 0.6B LM, offloaded"};
    if (gb < 16)
        return {"acestep-v15-turbo", "acestep-5Hz-lm-1.7B", true, false, "12-16 GB: turbo + 1.7B LM, LM offloaded"};
    if (gb < 20)
        return {"acestep-v15-xl-turbo", "acestep-5Hz-lm-1.7B", true, false, "16-20 GB: XL turbo with CPU offload"};
    if (gb < 24)
        return {"acestep-v15-xl-turbo", "acestep-5Hz-lm-1.7B", false, false, "20-24 GB: XL turbo + 1.7B LM"};
    return {"acestep-v15-xl-turbo", "acestep-5Hz-lm-4B", false, false, "24 GB+: XL turbo + 4B LM, no offload"};
}

AceStepModels effectiveAceStep(const MusicEngineConfig &config, int vramMb)
{
    auto models = recommendAceStep(vramMb);
    if (aceStepDitModels().contains(config.aceDit))
        models.dit = config.aceDit;
    if (aceStepLmModels().contains(config.aceLm))
        models.lm = config.aceLm;
    // XL needs CPU offload below 20 GB even when chosen manually.
    if (models.dit.contains("-xl-") && vramMb > 0 && vramMb < 20 * 1024)
        models.offloadToCpu = true;
    return models;
}

juce::File MusicEngines::aceStepRoot(const MusicEngineConfig &c)
{
    return c.aceStepInstall != juce::File{} ? c.aceStepInstall : managedAiRoot(c).getChildFile("ace-step");
}
juce::File MusicEngines::yueRoot(const MusicEngineConfig &c)
{
    return c.yueInstall != juce::File{} ? c.yueInstall : managedAiRoot(c).getChildFile("yue2");
}
bool MusicEngines::aceStepInstalled(const MusicEngineConfig &c)
{
#if JUCE_WINDOWS
    return findFile(aceStepRoot(c), "start_api_server.bat") != juce::File{};
#else
    juce::ignoreUnused(c);
    return false; // The managed portable installation is Windows-specific.
#endif
}
bool MusicEngines::yueInstalled(const MusicEngineConfig &c)
{
#if JUCE_WINDOWS
    return yueRoot(c).getChildFile(".venv/Scripts/yue2.exe").existsAsFile();
#else
    juce::ignoreUnused(c);
    return false;
#endif
}

MusicEngines::~MusicEngines()
{
    cancel();
    joinWorker();
    stopServer();
}

void MusicEngines::joinWorker()
{
    if (worker.joinable())
        worker.join();
}

bool MusicEngines::busy() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return state.state == JobState::running;
}

JobSnapshot MusicEngines::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return state;
}

void MusicEngines::cancel() noexcept
{
    cancelled.store(true);
}

void MusicEngines::update(JobState s, double progress, const juce::String &stage, const juce::String &message)
{
    std::lock_guard<std::mutex> lock(mutex);
    state.state = s;
    state.progress = progress;
    state.stage = stage;
    if (message.isNotEmpty())
        state.message = message;
}

void MusicEngines::finish(juce::Result result, const juce::File &audio)
{
    std::lock_guard<std::mutex> lock(mutex);
    state.state = result.wasOk() ? JobState::succeeded
                  : result.getErrorMessage() == "cancelled" ? JobState::cancelled
                                                            : JobState::failed;
    state.progress = result.wasOk() ? 1 : state.progress;
    state.message = result.wasOk() ? "Done" : result.getErrorMessage();
    state.audio = audio;
}

juce::Result MusicEngines::start(const MusicRequest &request, const MusicEngineConfig &config,
                                 const juce::File &outputFolder)
{
    if (auto valid = validateMusicRequest(request); valid.failed())
        return valid;
    if (request.engine == "yue2" && !config.yueLicenseAccepted)
        return juce::Result::fail("music.licenseRequired: Accept the YuE2 weights license (CC BY-NC 4.0) in Settings > AI");
    if (busy())
        return juce::Result::fail("music.busy: A music job is already running");
    joinWorker();
    cancelled.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = {};
        state.state = JobState::running;
        state.engine = request.engine;
        state.stage = "Starting";
        state.progress = -1;
    }
    worker = std::thread(
        [this, request, config, outputFolder]
        {
            juce::File audio;
            juce::Result result = juce::Result::ok();
            try
            {
                result = request.engine == "ace-step" ? generateAceStep(request, config, outputFolder, audio)
                                                      : generateYue(request, config, outputFolder, audio);
            }
            catch (const std::exception &error)
            {
                result = juce::Result::fail(juce::String("music.exception: ") + error.what());
            }
            finish(isCancelled() ? juce::Result::fail("cancelled") : result, audio);
        });
    return juce::Result::ok();
}

juce::Result MusicEngines::prepare(const juce::String &engine, const MusicEngineConfig &config)
{
    if (engine == "yue2" && !config.yueLicenseAccepted)
        return juce::Result::fail("music.licenseRequired: Accept the YuE2 weights license (CC BY-NC 4.0) first");
    if (busy())
        return juce::Result::fail("music.busy: A music job is already running");
    joinWorker();
    cancelled.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = {};
        state.state = JobState::running;
        state.engine = engine;
        state.stage = "Checking";
        state.progress = -1;
    }
    worker = std::thread(
        [this, engine, config]
        {
            juce::Result result = juce::Result::ok();
            try
            {
                result = engine == "ace-step" ? ensureAceStepServer(config) : ensureYue(config);
                if (result.wasOk() && engine == "yue2")
                {
                    const auto root = yueRoot(config);
                    result = runProcess({root.getChildFile(".venv/Scripts/yue2.exe").getFullPathName(), "doctor"},
                                        root, root.getChildFile("doctor.log"), 600000, "Checking YuE2 (yue2 doctor)");
                }
            }
            catch (const std::exception &error)
            {
                result = juce::Result::fail(juce::String("music.exception: ") + error.what());
            }
            finish(isCancelled() ? juce::Result::fail("cancelled") : result);
        });
    return juce::Result::ok();
}

juce::Result MusicEngines::checkHardware(const juce::String &engine, juce::int64 requiredFreeBytes,
                                         const juce::File &root)
{
    const auto gpu = detectGpu();
    if (!gpu.nvidia)
        return juce::Result::fail("music.noGpu: " + engine +
                                  " needs an NVIDIA GPU with current drivers (nvidia-smi was not found)");
    const int minimum = engine == "yue2" ? 8000 : 4000;
    if (gpu.vramMb > 0 && gpu.vramMb < minimum)
        return juce::Result::fail("music.lowVram: " + engine + " needs at least " + juce::String(minimum / 1000) +
                                  " GB VRAM; " + gpu.name + " has " + juce::String(gpu.vramMb / 1024.0, 1) + " GB");
    root.getParentDirectory().createDirectory();
    const auto free = root.getParentDirectory().getBytesFreeOnVolume();
    if (free > 0 && free < requiredFreeBytes)
        return juce::Result::fail("music.disk: " + engine + " needs about " + juce::String(requiredFreeBytes / gib) +
                                  " GB free; only " + juce::String(free / gib) + " GB available");
    return juce::Result::ok();
}

juce::Result MusicEngines::runProcess(const juce::StringArray &command, const juce::File &cwd, const juce::File &log,
                                      int timeoutMs, const juce::String &stage)
{
#if !JUCE_WINDOWS
    juce::ignoreUnused(command, cwd, log, timeoutMs, stage);
    return juce::Result::fail("music.platformUnsupported: Managed music-engine commands currently require Windows");
#else
    // A generated .cmd keeps quoting simple and sends all output to the log file.
    juce::String line;
    for (const auto &part : command)
        line << (line.isEmpty() ? "" : " ") << part.quoted();
    const auto script = cwd.getChildFile("vibera-step.cmd");
    cwd.createDirectory();
    script.replaceWithText("@echo off\r\ncd /d " + cwd.getFullPathName().quoted() + "\r\n" + line + " >> " +
                           log.getFullPathName().quoted() + " 2>&1\r\nexit /b %errorlevel%\r\n");
    update(JobState::running, -1, stage);
    WorkerProcess process;
    WorkerLimits limits;
    limits.containDescendants = true;
    if (!process.start({"cmd.exe", "/d", "/c", script.getFullPathName()}, 0, limits))
        return juce::Result::fail("music.process: Could not start " + command[0]);
    const auto deadline = juce::Time::getMillisecondCounter() + juce::uint32(timeoutMs);
    while (process.isRunning())
    {
        if (isCancelled())
        {
            process.kill();
            return juce::Result::fail("cancelled");
        }
        if (juce::Time::getMillisecondCounter() > deadline)
        {
            process.kill();
            return juce::Result::fail("music.timeout: " + stage + " timed out; see " + log.getFullPathName());
        }
        juce::Thread::sleep(200);
    }
    if (process.getExitCode() != 0)
        return juce::Result::fail("music.stepFailed: " + stage + " failed (exit " +
                                  juce::String(process.getExitCode()) + "); see " + log.getFullPathName());
    return juce::Result::ok();
#endif
}

juce::Result MusicEngines::ensureAceStep(const MusicEngineConfig &config)
{
#if !JUCE_WINDOWS
    juce::ignoreUnused(config);
    return juce::Result::fail(
        "music.platformUnsupported: Managed ACE-Step installation currently requires Windows. "
        "Start an ACE-Step API server separately and set its URL in Settings > AI");
#else
    if (aceStepInstalled(config))
        return juce::Result::ok();
    const auto root = aceStepRoot(config);
    if (auto ready = checkHardware("ACE-Step", 30 * gib, root); ready.failed())
        return ready;
    root.createDirectory();
    const auto archive = root.getChildFile("ACE-Step-1.5.7z");
    if (!archive.existsAsFile())
    {
        update(JobState::running, 0, "Downloading ACE-Step 1.5 (portable, several GB)");
        if (auto got = httpDownload(aceStepPortableUrl, archive,
                                    [this](double p, double rate)
                                    {
                                        update(JobState::running, p, "Downloading ACE-Step 1.5 (" +
                                                                         juce::String(rate / (1 << 20), 1) + " MB/s)");
                                    },
                                    [this] { return isCancelled(); });
            got.failed())
            return got;
    }
    // Windows 10+ tar.exe (libarchive) extracts 7z archives.
    if (auto extracted = runProcess({systemTar(), "-xf", archive.getFileName()}, root,
                                    root.getChildFile("install.log"), 30 * 60000, "Extracting ACE-Step 1.5");
        extracted.failed())
        return extracted;
    if (!aceStepInstalled(config))
        return juce::Result::fail("music.install: ACE-Step archive did not contain start_api_server.bat");
    archive.deleteFile(); // Reproducible download; the extracted install is what is kept.
    return juce::Result::ok();
#endif
}

bool MusicEngines::serverRunning() const
{
    return server != nullptr; // Owned server handle; health is checked separately.
}

void MusicEngines::stopServer()
{
    if (!server)
        return;
    server->kill(); // Owned job object: closing it ends the server and its descendants.
    server.reset();
}

juce::Result MusicEngines::ensureAceStepServer(const MusicEngineConfig &config)
{
    const auto health = [&]
    { return httpRequest("GET", apiBase(config) + "/health", {}, {}, 3000, [this] { return isCancelled(); }).ok(); };
    const auto wanted = effectiveAceStep(config, cachedGpu().vramMb);
    // A model change restarts only the server VibeRa owns; an external server is used as-is.
    if (server && (wanted.dit != serverModels.dit || wanted.lm != serverModels.lm ||
                   wanted.offloadToCpu != serverModels.offloadToCpu))
        stopServer();
    if (health())
        return juce::Result::ok();
    if (!config.aceStepAutoStart)
        return juce::Result::fail("music.serverDown: ACE-Step API is not reachable at " + config.aceStepUrl);
    if (auto installed = ensureAceStep(config); installed.failed())
        return installed;
    const auto launcher = findFile(aceStepRoot(config), "start_api_server.bat");
    const auto log = aceStepRoot(config).getChildFile("server.log");
    {
        // The model repos use Hugging Face Xet storage; without hf_xet the portable Python
        // falls back to slow plain HTTP for ~10-36 GB of weights. Best effort, once.
        const auto folder = launcher.getParentDirectory();
        const auto python = folder.getChildFile("python_embeded/python.exe");
        const auto marker = folder.getChildFile(".vibera-hf-xet");
        if (python.existsAsFile() && !marker.existsAsFile())
        {
            if (runProcess({python.getFullPathName(), "-m", "pip", "install", "--upgrade", "hf_xet"}, folder,
                           aceStepRoot(config).getChildFile("install.log"), 10 * 60000,
                           "Installing the fast Hugging Face downloader (hf_xet)")
                    .wasOk())
                marker.create();
            if (isCancelled())
                return juce::Result::fail("cancelled");
        }
    }
    if (!server)
    {
        auto owned = std::make_unique<WorkerProcess>();
        WorkerLimits limits;
        limits.containDescendants = true;
        const auto script = launcher.getParentDirectory().getChildFile("vibera-server.cmd");
        // Model choice goes through the documented environment variables (the launcher
        // passes no DiT flag; it reads ACESTEP_CONFIG_PATH / ACESTEP_LM_MODEL_PATH).
        juce::String env;
        env << "set ACESTEP_CONFIG_PATH=" << wanted.dit << "\r\n";
        if (wanted.lm != "none")
            env << "set ACESTEP_LM_MODEL_PATH=" << wanted.lm << "\r\n";
        env << "set ACESTEP_OFFLOAD_TO_CPU=" << (wanted.offloadToCpu ? "true" : "false") << "\r\n";
        env << "set ACESTEP_OFFLOAD_DIT_TO_CPU=" << (wanted.offloadDitToCpu ? "true" : "false") << "\r\n";
        // Run the portable embedded Python directly: the stock launcher runs a git update check
        // that can stop at an interactive "Update now? (Y/N)" prompt.
        const auto folder = launcher.getParentDirectory();
        const auto python = folder.getChildFile("python_embeded/python.exe");
        const auto entry = folder.getChildFile("acestep/api_server.py");
        const juce::URL endpoint(config.aceStepUrl);
        const auto host = endpoint.getDomain().isNotEmpty() ? endpoint.getDomain() : juce::String("127.0.0.1");
        const auto port = endpoint.getPort() > 0 ? juce::String(endpoint.getPort()) : juce::String("8001");
        const auto run = python.existsAsFile() && entry.existsAsFile()
                             ? python.getFullPathName().quoted() + " " + entry.getFullPathName().quoted() + " --host " +
                                   host + " --port " + port
                             : "call " + launcher.getFullPathName().quoted();
        script.replaceWithText("@echo off\r\ncd /d " + folder.getFullPathName().quoted() + "\r\n" + env + run + " >> " +
                               log.getFullPathName().quoted() + " 2>&1\r\n");
        if (!owned->start({"cmd.exe", "/d", "/c", script.getFullPathName()}, 0, limits))
            return juce::Result::fail("music.serverStart: Could not start the ACE-Step API server");
        server = std::move(owned);
        serverModels = wanted;
    }
    // First start downloads model weights, which can take a long time.
    const auto deadline = juce::Time::getMillisecondCounter() + 45u * 60000u;
    while (!health())
    {
        if (isCancelled())
            return juce::Result::fail("cancelled");
        if (!server->isRunning())
        {
            server.reset();
            return juce::Result::fail("music.serverExited: ACE-Step server stopped; see " + log.getFullPathName());
        }
        if (juce::Time::getMillisecondCounter() > deadline)
            return juce::Result::fail("music.serverTimeout: ACE-Step did not become ready; see " + log.getFullPathName());
        update(JobState::running, -1, "Starting ACE-Step " + wanted.dit + " (first run downloads models)");
        juce::Thread::sleep(2000);
    }
    // Load the chosen models explicitly (the launcher may default the LM); /v1/init switches at runtime.
    juce::StringPairArray headers;
    headers.set("Content-Type", "application/json");
    auto init = juce::var(new juce::DynamicObject);
    init.getDynamicObject()->setProperty("model", wanted.dit);
    init.getDynamicObject()->setProperty("init_llm", wanted.lm != "none");
    if (wanted.lm != "none")
        init.getDynamicObject()->setProperty("lm_model_path", wanted.lm);
    update(JobState::running, -1, "Loading " + wanted.dit + (wanted.lm != "none" ? " + " + wanted.lm : juce::String{}));
    const auto loaded = httpRequest("POST", apiBase(config) + "/v1/init", headers, juce::JSON::toString(init, true),
                                    45 * 60000, [this] { return isCancelled(); });
    if (!loaded.ok())
        return juce::Result::fail("music.modelLoad: ACE-Step could not load " + wanted.dit + ": " +
                                  describeHttpFailure(loaded));
    return juce::Result::ok();
}

juce::Result MusicEngines::generateAceStep(const MusicRequest &request, const MusicEngineConfig &config,
                                           const juce::File &out, juce::File &audio)
{
    if (auto ready = ensureAceStepServer(config); ready.failed())
        return ready;
    update(JobState::running, -1, "Generating with ACE-Step");
    juce::StringPairArray headers;
    headers.set("Content-Type", "application/json");
    const auto cancel = [this] { return isCancelled(); };
    const auto released = httpRequest("POST", apiBase(config) + "/release_task", headers,
                                      juce::JSON::toString(aceStepTaskBody(request), true), 60000, cancel);
    if (!released.ok())
        return juce::Result::fail("music.aceStep: " + describeHttpFailure(released));
    const auto json = juce::JSON::parse(released.body);
    const auto taskId = (json.hasProperty("data") ? json["data"]["task_id"] : json["task_id"]).toString();
    if (taskId.isEmpty())
        return juce::Result::fail("music.aceStep: No task_id in release_task response");
    const auto deadline = juce::Time::getMillisecondCounter() + 30u * 60000u;
    juce::var result;
    for (;;)
    {
        if (isCancelled())
            return juce::Result::fail("cancelled");
        if (juce::Time::getMillisecondCounter() > deadline)
            return juce::Result::fail("music.timeout: ACE-Step task did not finish in 30 minutes");
        auto query = object();
        set(query, "task_id_list", juce::Array<juce::var>{taskId});
        const auto polled = httpRequest("POST", apiBase(config) + "/query_result", headers,
                                        juce::JSON::toString(query, true), 30000, cancel);
        if (polled.ok())
        {
            result = juce::JSON::parse(polled.body);
            const auto status = aceStepTaskStatus(result, taskId);
            if (status == 1)
                break;
            if (status == 2)
                return juce::Result::fail("music.aceStep: Generation failed: " + polled.body.substring(0, 300));
        }
        juce::Thread::sleep(1500);
    }
    const auto references = aceStepAudioReferences(result);
    if (references.isEmpty())
        return juce::Result::fail("music.aceStep: Task finished without an audio file");
    out.createDirectory();
    audio = out.getChildFile("ace-step-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav")
                .getNonexistentSibling();
    update(JobState::running, -1, "Downloading generated audio");
    return httpDownload(apiBase(config) + references[0], audio, {}, cancel);
}

juce::Result MusicEngines::ensureYue(const MusicEngineConfig &config)
{
#if !JUCE_WINDOWS
    juce::ignoreUnused(config);
    return juce::Result::fail("music.platformUnsupported: Managed YuE2 installation and launch currently require Windows");
#else
    if (yueInstalled(config))
        return juce::Result::ok();
    const auto root = yueRoot(config);
    if (auto ready = checkHardware("yue2", 40 * gib, root); ready.failed())
        return ready;
    root.createDirectory();
    const auto log = root.getChildFile("install.log");
    const auto tools = root.getChildFile("tools");
    const auto uv = tools.getChildFile("uv.exe");
    const auto cancel = [this] { return isCancelled(); };
    if (!uv.existsAsFile())
    {
        const auto zip = tools.getChildFile("uv.zip");
        update(JobState::running, -1, "Downloading uv (Python manager)");
        if (auto got = httpDownload(uvUrl, zip, {}, cancel); got.failed())
            return got;
        if (auto unpacked = runProcess({systemTar(), "-xf", zip.getFileName()}, tools, log, 300000, "Unpacking uv");
            unpacked.failed())
            return unpacked;
        zip.deleteFile();
        if (!uv.existsAsFile())
            return juce::Result::fail("music.install: uv.exe missing after extraction");
    }
    const auto source = root.getChildFile("YuE-main");
    if (!source.getChildFile("pyproject.toml").existsAsFile())
    {
        const auto zip = root.getChildFile("yue2.zip");
        update(JobState::running, -1, "Downloading YuE2 source");
        if (auto got = httpDownload(yueRepoZip, zip, {}, cancel); got.failed())
            return got;
        if (auto unpacked = runProcess({systemTar(), "-xf", zip.getFileName()}, root, log, 600000, "Unpacking YuE2");
            unpacked.failed())
            return unpacked;
        zip.deleteFile();
    }
    const auto venv = root.getChildFile(".venv");
    const auto python = venv.getChildFile("Scripts/python.exe").getFullPathName();
    const juce::StringArray steps[] = {
        {uv.getFullPathName(), "python", "install", "3.12"},
        {uv.getFullPathName(), "venv", "--python", "3.12", venv.getFullPathName()},
        {uv.getFullPathName(), "pip", "install", "--python", python, "torch==2.10.0", "--index-url",
         "https://download.pytorch.org/whl/cu128"},
        {uv.getFullPathName(), "pip", "install", "--python", python, source.getFullPathName()},
    };
    const char *labels[] = {"Installing Python 3.12", "Creating YuE2 environment",
                            "Installing PyTorch (CUDA 12.8, several GB)", "Installing YuE2"};
    for (size_t i = 0; i < std::size(steps); ++i)
        if (auto done = runProcess(steps[i], root, log, 90 * 60000, labels[i]); done.failed())
            return done;
    return yueInstalled(config) ? juce::Result::ok()
                                : juce::Result::fail("music.install: yue2.exe missing after install; see " +
                                                     log.getFullPathName());
#endif
}

juce::Result MusicEngines::generateYue(const MusicRequest &request, const MusicEngineConfig &config,
                                       const juce::File &out, juce::File &audio)
{
    if (auto ready = ensureYue(config); ready.failed())
        return ready;
    const auto root = yueRoot(config);
    const auto job = out.getChildFile("yue2-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"))
                         .getNonexistentSibling();
    job.createDirectory();
    const auto requestFile = job.getChildFile("request.json");
    requestFile.replaceWithText(juce::JSON::toString(yueRequestBody(request)));
    juce::StringArray command{root.getChildFile(".venv/Scripts/yue2.exe").getFullPathName(), "generate", "--request",
                              requestFile.getFullPathName(), "--output", job.getChildFile("song").getFullPathName(),
                              "--device", "cuda"};
    if (config.yueLowVram)
        command.addArray({"--low-vram", "--budget", "8"});
    // First run downloads the m-a-p/YuE2-3B weights from Hugging Face.
    if (auto done = runProcess(command, root, job.getChildFile("generate.log"), 120 * 60000,
                               "Generating with YuE2 (first run downloads models)");
        done.failed())
        return done;
    audio = job.getChildFile("song/audio.flac");
    return audio.existsAsFile() ? juce::Result::ok()
                                : juce::Result::fail("music.yue: No audio.flac produced; see " +
                                                     job.getChildFile("generate.log").getFullPathName());
}
} // namespace vibera::ai
