// SPDX-License-Identifier: AGPL-3.0-or-later
// Included in StudioMcp.cpp; all operations run through canonical CONTROL services.
juce::var Studio::mcpSynthOperation(const juce::var &args)
{
    using namespace synth;
    const auto action=args["action"].toString();
    auto state=readSynthBrowserState();
    auto failed=[](const juce::Result &r) { if(r.failed()) throw McpFailure("synth.refused",r.getErrorMessage()); };
    if(action=="descriptor")
    { auto out=obj(); set(out,"descriptor",nativeDescription().createXml()->toString()); return textResult(out); }
    if(action=="listUserPresets")
    {
        std::vector<SynthUserPresetMetadata> presets;juce::StringArray errors;
        failed(listSynthUserPresets(synthUserDirectory(),presets,errors));
        auto out=obj();juce::Array<juce::var> items,messages;
        for(const auto &p:presets) items.add(p.toVar());
        for(const auto &error:errors) messages.add(error);
        set(out,"presets",items);set(out,"errors",messages);return textResult(out);
    }
    if(action=="list")
    {
        if(args.hasProperty("genre")) state.genre=args["genre"].toString();
        if(args.hasProperty("family")) state.family=args["family"].toString();
        if(args.hasProperty("search")) state.search=args["search"].toString();
        if(args.hasProperty("favoritesOnly")) state.favouritesOnly=bool(args["favoritesOnly"]);
        failed(validateSynthBrowserState(state));
        return textResult(synthDiscovery(state));
    }
    if(action=="browser" || action=="filter" || action=="favorite" || action=="navigate")
    {
        if(action=="filter")
        {
            if(args.hasProperty("genre")) state.genre=args["genre"].toString();
            if(args.hasProperty("family")) state.family=args["family"].toString();
            if(args.hasProperty("search")) state.search=args["search"].toString();
            if(args.hasProperty("favoritesOnly")) state.favouritesOnly=bool(args["favoritesOnly"]);
        }
        if(action=="navigate")
        {
            if(args.hasProperty("presetScrollY")) state.presetScrollY=integer(args,"presetScrollY",0,0,10000);
            if(args.hasProperty("effectPage")) state.effectPage=integer(args,"effectPage",0,0,4);
            if(args.hasProperty("octave")) state.octave=integer(args,"octave",0,-1,4);
            if(args.hasProperty("scrollX")) state.scrollX=integer(args,"scrollX",0,0,10000);
            if(args.hasProperty("scrollY")) state.scrollY=integer(args,"scrollY",0,0,10000);
            if(args.hasProperty("selectedPresetId")) state.selectedPresetId=args["selectedPresetId"].toString();
        }
        if(action=="favorite")
        {
            const auto preset=args["preset"].toString();
            if(!findFactoryPreset(preset.toStdString()) || !args.hasProperty("enabled"))
                throw McpFailure("synth.invalidFavorite","Exact preset ID and enabled are required");
            if(bool(args["enabled"])) state.favouriteIds.addIfNotAlreadyThere(preset);
            else state.favouriteIds.removeString(preset);
        }
        if(action!="browser") failed(writeSynthBrowserState(state));
        return textResult(synthDiscovery(state));
    }
    if(action=="create")
    {
        const auto count=controller.project.channels.size();
        auto request=obj(),arguments=obj();
        set(arguments,"text",nativeDescription().createXml()->toString());
        set(request,"command","plugin.instrument.create");set(request,"args",arguments);
        auto response=mcpCall("vibera_command",request);
        auto out=response["structuredContent"];
        if(!bool(response["isError"]) && out["completionState"].toString()=="completed")
        {
            const auto &channels=controller.project.channels;
            if(channels.size()!=count+1 || !channels.back().instrument ||
                channels.back().instrument->descriptor!=arguments["text"].toString())
                throw McpFailure("synth.createRefused","Canonical create did not create the requested instrument");
            set(out,"channel",juce::int64(channels.back().id));
            set(out,"plugin",juce::int64(channels.back().instrument->id));
            set(out,"audioPreparation","inspect vibera_plugin list for ready instance");
        }
        else if(out["completionState"].toString()=="pending") set(out,"pollTool","vibera_mixerchain");
        if(bool(response["isError"])) return response;
        return textResult(out);
    }
    const auto id=identity(args,"plugin",0);
    const auto *spec=controller.project.plugin(id);
    juce::PluginDescription description;
    auto xml=spec?juce::XmlDocument::parse(spec->descriptor):nullptr;
    if(!spec || !xml || !description.loadFromXml(*xml) || !isNativeDescription(description))
        throw McpFailure("synth.invalidTarget","An existing VibeRa Synthesizer plugin ID is required");
    if(action=="previewOn" || action=="previewOff" || action=="panic")
    {
        Id channel=0;
        for(const auto &c:controller.project.channels) if(c.instrument && c.instrument->id==id) channel=c.id;
        if(!channel) throw McpFailure("synth.invalidTarget","Instrument Channel missing");
        if(action=="panic") audio.stopChannelPreview(channel);
        else if(!audio.previewChannelNote(channel,integer(args,"note",-1,0,127),action=="previewOn",
                                         integer(args,"velocity",100,1,127)))
            throw McpFailure("synth.previewRefused","Wait for instrument audio preparation");
        auto out=obj(); set(out,"plugin",juce::int64(id)); set(out,"previewLaneOnly",true); return textResult(out);
    }
    if(action=="parameters" || action=="status" || action=="openEditor" || action=="closeEditor" || action=="set")
    {
        auto forwarded=obj(); set(forwarded,"plugin",juce::int64(id));
        if(args.hasProperty("instance")) set(forwarded,"instance",args["instance"]);
        // Parameter pages hold 64 rows by default: offset/limit reach every exposed scalar.
        if(action=="parameters" && args.hasProperty("offset")) set(forwarded,"offset",args["offset"]);
        if(action=="parameters" && args.hasProperty("limit")) set(forwarded,"limit",args["limit"]);
        set(forwarded,"action",action=="status"?"parameters":action);
        if(action=="set")
        {
            const auto key=args["key"].toString();
            const ParameterSpec *parameter=nullptr;
            for(const auto &p:parameterSpecs()) if(key==p.key) parameter=&p;
            if(!parameter || parameter->id==ParameterId::factoryPreset)
                throw McpFailure("synth.unknownParameter","Discover an exposed key; use loadPreset for a complete patch");
            const double value=number(args,"value",std::numeric_limits<double>::quiet_NaN());
            // Discovery serialises float endpoints through JSON decimal text.
            // Permit only double-roundtrip precision at those exact endpoints.
            const double tolerance=8*std::numeric_limits<double>::epsilon()*std::max({1.0,std::abs(double(parameter->minimum)),std::abs(double(parameter->maximum))});
            if(!std::isfinite(value) || value<double(parameter->minimum)-tolerance || value>double(parameter->maximum)+tolerance ||
               (parameter->interval>0 && std::abs((value-parameter->minimum)/parameter->interval-std::round((value-parameter->minimum)/parameter->interval))>1e-5))
                throw McpFailure("synth.parameterRange","Value must use the discovered native units and interval");
            juce::NormalisableRange<float> range(parameter->minimum,parameter->maximum,parameter->interval,parameter->skew);
            set(forwarded,"generation",args["generation"]); set(forwarded,"parameter",juce::int64(0x10000+parameterIndex(parameter->id)));
            set(forwarded,"value",range.convertTo0to1(float(value)));
        }
        auto response=mcpPluginOperation(forwarded);
        if(action=="status")
        {
            const auto host=audio.preparedPluginHost(controller.project,id);
            auto result=response["structuredContent"];
            set(result,"synthesizer",synthTelemetryJson(host?host->nativeOutputMeter().synthState:0));
            return textResult(result);
        }
        return response;
    }
    if(action=="previous" || action=="next")
    {
        const auto current=args.hasProperty("preset")?args["preset"].toString():state.selectedPresetId;
        if(current.isNotEmpty() && !findFactoryPreset(current.toStdString())) throw McpFailure("synth.unknownPreset","Current preset ID is unknown");
        const auto adjacent=adjacentSynthPresetId(state,current,action=="next"?1:-1);
        if(adjacent.isEmpty()) throw McpFailure("synth.emptyFilter","No factory presets match the current browser filters");
        auto request=args.clone();request.getDynamicObject()->setProperty("action","loadPreset");
        request.getDynamicObject()->setProperty("preset",adjacent);
        return mcpSynthOperation(request);
    }
    if(action=="loadPreset" || action=="loadUserPreset")
    {
        std::vector<std::pair<std::uint32_t,float>> values;
        if(action=="loadPreset")
        {
            const auto *preset=findFactoryPreset(args["preset"].toString().toStdString());
            if(!preset) throw McpFailure("synth.unknownPreset","Rediscover factory preset IDs");
            for(const auto &p:parameterSpecs())
            {
                juce::NormalisableRange<float> range(p.minimum,p.maximum,p.interval,p.skew);
                values.emplace_back(0x10000+std::uint32_t(parameterIndex(p.id)),range.convertTo0to1(preset->values[parameterIndex(p.id)]));
            }
        }
        else
        {
            const auto name=args["name"].toString(), presetId=args["preset"].toString();
            juce::File file;
            if(presetId.isNotEmpty())
            {
                if(name.isNotEmpty()) throw McpFailure("synth.userIdentity","Use either stable user preset ID or basename");
                std::vector<SynthUserPresetMetadata> presets;juce::StringArray errors;
                failed(listSynthUserPresets(synthUserDirectory(),presets,errors));
                const auto found=std::find_if(presets.begin(),presets.end(),[&](const auto &p){return p.id==presetId;});
                if(found==presets.end()) throw McpFailure("synth.unknownUserPreset","Rediscover listUserPresets including malformed-file errors");
                file=found->file;
            }
            else
            {
                if(name.isEmpty() || name.containsAnyOf("/\\:<>|?*\".") || name.length()>100)
                    throw McpFailure("synth.userName","A plain preset basename without extension is required");
                file=synthUserDirectory().getChildFile(name+".vrsynth");
            }
            SynthUserPreset preset;
            failed(readSynthUserPreset(file,preset));
            for(std::size_t i=0;i<parameterCount;++i)
                values.emplace_back(synthParameterBaseId+std::uint32_t(i),preset.normalizedValues[i]);
        }
        juce::String operation;
        failed(requestNativePatch(id,unsigned(number(args,"generation",0)),args["instance"].toString(),values,operation));
        auto out=obj(); set(out,"operation",operation); set(out,"completionState","pending");
        set(out,"pollTool","vibera_plugin"); set(out,"historyCommitted",false); return textResult(out);
    }
    if(action=="saveUserPreset")
    {
        const auto name=args["name"].toString();
        if(name.isEmpty() || name.containsAnyOf("/\\:<>|?*\".") || name.length()>100)
            throw McpFailure("synth.userName","A plain preset basename without extension is required");
        if(synthUserDirectory().getChildFile(name+".vrsynth").exists() && !bool(args.getProperty("overwrite",false)))
            throw McpFailure("synth.overwriteRequired","User preset exists; choose another name or explicitly set overwrite=true");
        const auto host=audio.preparedPluginHost(controller.project,id);
        if(!host || host->runtimeIdentity()!=args["instance"].toString()) throw McpFailure("synth.staleInstance","Exact current instance required");
        PluginOperation job; job.id=juce::Uuid().toString(); job.kind="synthSave"; job.plugin=id;
        job.project=controller.projectIdentity; job.instance=host->runtimeIdentity(); job.started=juce::Time::getMillisecondCounter();
        if(pluginOperations.size()>=64)
        {
            const auto terminal=std::find_if(pluginOperations.begin(),pluginOperations.end(),[](const auto &j){return j.state!="pending";});
            if(terminal==pluginOperations.end()) throw McpFailure("synth.capacity","Observe outstanding plugin jobs first");
            pluginOperations.erase(terminal);
        }
        const auto token=job.id; pluginOperations.push_back(std::move(job));
        const std::vector<Id> ids{id};
        const bool overwrite=bool(args.getProperty("overwrite",false));
        try {
        withPluginState([this,id,name,token,overwrite](juce::Result result,Project captured)
        {
            auto found=std::find_if(pluginOperations.begin(),pluginOperations.end(),[&](const auto &j){return j.id==token;});
            if(found==pluginOperations.end()) return;
            if(result.wasOk())
            {
                const auto *p=captured.plugin(id); juce::MemoryBlock data;
                if(!p || !decodePluginState(p->state,data)) result=juce::Result::fail("Cannot decode captured synthesizer state");
                else if(!overwrite && synthUserDirectory().getChildFile(name+".vrsynth").exists())
                    result=juce::Result::fail("User preset appeared during capture; choose another name or explicitly overwrite");
                else result=writeSynthUserPreset(synthUserDirectory().getChildFile(name+".vrsynth"),data);
            }
            found->state=result.wasOk()?"completed":"failed"; found->code=result.wasOk()?"":"synth.presetWrite";
            found->message=result.wasOk()?"Captured sound saved to user preset":result.getErrorMessage();
            found->completedRevision=controller.revision;
        },&ids);
        } catch(const std::exception &error) {
            auto found=std::find_if(pluginOperations.begin(),pluginOperations.end(),[&](const auto &j){return j.id==token;});
            if(found!=pluginOperations.end()) {found->state="failed";found->code="synth.captureRefused";found->message=juce::String::fromUTF8(error.what());found->completedRevision=controller.revision;}
        }
        auto out=obj(); set(out,"operation",token); set(out,"completionState","pending"); set(out,"pollTool","vibera_plugin"); return textResult(out);
    }
    throw McpFailure("synth.invalidAction","Rediscover VibeRa Synthesizer actions");
}
