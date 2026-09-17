#include "kscriptmanager.h"

namespace kemena
{
    // -----------------------------------------------------------------------
    // Lifecycle event declarations
    // -----------------------------------------------------------------------

    const char *kScriptEventDecl(kScriptEvent evt)
    {
        switch (evt)
        {
            case K_SCRIPT_AWAKE:        return "void Awake()";
            case K_SCRIPT_START:        return "void Start()";
            case K_SCRIPT_UPDATE:       return "void Update()";
            case K_SCRIPT_FIXED_UPDATE: return "void FixedUpdate()";
            case K_SCRIPT_LATE_UPDATE:  return "void LateUpdate()";
            case K_SCRIPT_ON_ENABLE:    return "void OnEnable()";
            case K_SCRIPT_ON_DISABLE:   return "void OnDisable()";
            case K_SCRIPT_ON_DESTROY:   return "void OnDestroy()";
            // Collision/trigger events receive the object they touched.
            case K_SCRIPT_ON_COLLISION_ENTER: return "void OnCollisionEnter(kObject@ other)";
            case K_SCRIPT_ON_COLLISION_STAY:  return "void OnCollisionStay(kObject@ other)";
            case K_SCRIPT_ON_COLLISION_EXIT:  return "void OnCollisionExit(kObject@ other)";
            case K_SCRIPT_ON_TRIGGER_ENTER:   return "void OnTriggerEnter(kObject@ other)";
            case K_SCRIPT_ON_TRIGGER_STAY:    return "void OnTriggerStay(kObject@ other)";
            case K_SCRIPT_ON_TRIGGER_EXIT:    return "void OnTriggerExit(kObject@ other)";
            default:                          return "";
        }
    }

    // -----------------------------------------------------------------------
    // kFileByteStream
    // -----------------------------------------------------------------------

    kFileByteStream::kFileByteStream(const kString &path, bool writing)
    {
        file = std::fopen(path.c_str(), writing ? "wb" : "rb");
    }

    kFileByteStream::~kFileByteStream()
    {
        if (file)
            std::fclose(file);
    }

    int kFileByteStream::Read(void *ptr, asUINT size)
    {
        if (!file || size == 0)
            return size == 0 ? 0 : -1;
        return std::fread(ptr, 1, size, file) == size ? 0 : -1;
    }

    int kFileByteStream::Write(const void *ptr, asUINT size)
    {
        if (!file || size == 0)
            return size == 0 ? 0 : -1;
        return std::fwrite(ptr, 1, size, file) == size ? 0 : -1;
    }

    // -----------------------------------------------------------------------
    // Engine message callback
    // -----------------------------------------------------------------------

    // Process-wide host sink for compiler messages (see setMessageHandler()).
    static kScriptManager::MessageHandler g_messageHandler;

    // Process-wide host sink for runtime script print output.
    static kScriptManager::ScriptPrintHandler g_scriptPrintHandler;

    static void messageCallback(const asSMessageInfo *msg, void * /*param*/)
    {
        const char *type = "ERR ";
        int severity = kScriptManager::MSG_ERROR;
        if (msg->type == asMSGTYPE_WARNING)      { type = "WARN"; severity = kScriptManager::MSG_WARNING; }
        else if (msg->type == asMSGTYPE_INFORMATION) { type = "INFO"; severity = kScriptManager::MSG_INFO; }
        printf("%s (%d, %d) : %s : %s\n", msg->section, msg->row, msg->col, type, msg->message);

        if (g_messageHandler)
            g_messageHandler(severity, msg->section, msg->row, msg->col, msg->message);
    }

    void kScriptManager::setMessageHandler(MessageHandler handler)
    {
        g_messageHandler = std::move(handler);
    }

    void kScriptManager::setScriptPrintHandler(ScriptPrintHandler handler)
    {
        g_scriptPrintHandler = std::move(handler);
    }

    void kScriptManager::handleScriptPrint(const char *message)
    {
        if (g_scriptPrintHandler)
        {
            g_scriptPrintHandler(message ? message : "");
            return;
        }
        printf("[Script] %s\n", message ? message : "");
    }

    // -----------------------------------------------------------------------
    // kScriptManager — construction
    // -----------------------------------------------------------------------

    kScriptManager::kScriptManager()
    {
        engine = asCreateScriptEngine();

        int result = engine->SetMessageCallback(asFUNCTION(messageCallback), 0, asCALL_CDECL);
        assert(result >= 0);
        (void)result;

        RegisterStdString(engine);

        context = engine->CreateContext();
    }

    kScriptManager::~kScriptManager()
    {
        destroy();
    }

    // -----------------------------------------------------------------------
    // Script-asset registry
    // -----------------------------------------------------------------------

    void kScriptManager::registerScriptAsset(const kString &uuid, const kString &sourcePath,
                                             kScriptSourceType type, const kString &bytecodePath)
    {
        kScriptAsset &asset = assets[uuid];
        asset.uuid         = uuid;
        asset.sourcePath   = sourcePath;
        asset.bytecodePath = bytecodePath;
        asset.sourceType   = type;
    }

    kScriptAsset *kScriptManager::getScriptAsset(const kString &uuid)
    {
        auto it = assets.find(uuid);
        return it == assets.end() ? nullptr : &it->second;
    }

    void kScriptManager::unregisterScriptAsset(const kString &uuid)
    {
        assets.erase(uuid);
    }

    void kScriptManager::setBytecodePath(const kString &uuid, const kString &bytecodePath)
    {
        auto it = assets.find(uuid);
        if (it != assets.end())
            it->second.bytecodePath = bytecodePath;
    }

    // -----------------------------------------------------------------------
    // Compilation
    // -----------------------------------------------------------------------

    bool kScriptManager::compileFileToBytecode(const kString &sourcePath, const kString &outPath,
                                               bool stripDebug)
    {
        const kString tmpName = "kcompile_" + std::to_string(moduleCounter++);

        CScriptBuilder builder;
        if (builder.StartNewModule(engine, tmpName.c_str()) < 0)
        {
            printf("Script: out of memory while starting module for '%s'.\n", sourcePath.c_str());
            return false;
        }
        if (builder.AddSectionFromFile(sourcePath.c_str()) < 0)
        {
            printf("Script: failed to read source file '%s'.\n", sourcePath.c_str());
            engine->DiscardModule(tmpName.c_str());
            return false;
        }
        if (builder.BuildModule() < 0)
        {
            printf("Script: compilation failed for '%s'.\n", sourcePath.c_str());
            engine->DiscardModule(tmpName.c_str());
            return false;
        }

        asIScriptModule *module = engine->GetModule(tmpName.c_str());
        if (!module)
        {
            engine->DiscardModule(tmpName.c_str());
            return false;
        }

        kFileByteStream out(outPath, true);
        if (!out.isOpen())
        {
            printf("Script: cannot open bytecode output '%s'.\n", outPath.c_str());
            engine->DiscardModule(tmpName.c_str());
            return false;
        }

        int saved = module->SaveByteCode(&out, stripDebug);
        engine->DiscardModule(tmpName.c_str());

        if (saved < 0)
        {
            printf("Script: SaveByteCode failed for '%s'.\n", sourcePath.c_str());
            return false;
        }
        return true;
    }

    bool kScriptManager::compileToBytecode(const kString &scriptUuid, const kString &outPath,
                                           bool stripDebug)
    {
        kScriptAsset *asset = getScriptAsset(scriptUuid);
        if (!asset)
        {
            printf("Script: cannot compile unregistered asset '%s'.\n", scriptUuid.c_str());
            return false;
        }

        if (!compileFileToBytecode(asset->sourcePath, outPath, stripDebug))
            return false;

        asset->bytecodePath = outPath;
        asset->checksum     = generateFileChecksum(asset->sourcePath);
        return true;
    }

    // -----------------------------------------------------------------------
    // Per-object instances
    // -----------------------------------------------------------------------

    bool kScriptManager::buildInstanceModule(kScriptInstance *inst, const kScriptAsset &asset)
    {
        const kString name = "kmod_" + inst->componentUuid;
        engine->DiscardModule(name.c_str()); // harmless if it does not exist

        // Prefer precompiled bytecode.
        if (!asset.bytecodePath.empty())
        {
            kFileByteStream in(asset.bytecodePath, false);
            if (in.isOpen())
            {
                asIScriptModule *module = engine->GetModule(name.c_str(), asGM_ALWAYS_CREATE);
                if (module && module->LoadByteCode(&in) >= 0)
                {
                    inst->module     = module;
                    inst->moduleName = name;
                    return true;
                }
                printf("Script: bytecode load failed for '%s', falling back to source.\n",
                       asset.bytecodePath.c_str());
                engine->DiscardModule(name.c_str());
            }
        }

        // Fall back to compiling the source text directly.
        if (asset.sourcePath.empty())
            return false;

        CScriptBuilder builder;
        if (builder.StartNewModule(engine, name.c_str()) < 0)
            return false;
        if (builder.AddSectionFromFile(asset.sourcePath.c_str()) < 0)
        {
            engine->DiscardModule(name.c_str());
            return false;
        }
        if (builder.BuildModule() < 0)
        {
            engine->DiscardModule(name.c_str());
            return false;
        }

        inst->module     = engine->GetModule(name.c_str());
        inst->moduleName = name;
        return inst->module != nullptr;
    }

    void kScriptManager::cacheInstanceFunctions(kScriptInstance *inst)
    {
        if (!inst->module)
            return;
        for (int e = 0; e < K_SCRIPT_EVENT_COUNT; ++e)
        {
            inst->fn[e] = inst->module->GetFunctionByDecl(
                kScriptEventDecl(static_cast<kScriptEvent>(e)));
        }
    }

    kScriptInstance *kScriptManager::createInstance(const kString &componentUuid,
                                                    const kString &scriptUuid)
    {
        // Replace any previous instance bound to the same component slot.
        destroyInstance(componentUuid);

        kScriptAsset *asset = getScriptAsset(scriptUuid);
        if (!asset)
        {
            printf("Script: createInstance — unregistered asset '%s'.\n", scriptUuid.c_str());
            return nullptr;
        }

        kScriptInstance *inst = new kScriptInstance();
        inst->componentUuid = componentUuid;
        inst->scriptUuid    = scriptUuid;

        if (!buildInstanceModule(inst, *asset))
        {
            printf("Script: createInstance — build failed for component '%s'.\n",
                   componentUuid.c_str());
            delete inst;
            return nullptr;
        }

        cacheInstanceFunctions(inst);
        inst->valid = true;
        instances[componentUuid] = inst;
        return inst;
    }

    kScriptInstance *kScriptManager::getInstance(const kString &componentUuid)
    {
        auto it = instances.find(componentUuid);
        return it == instances.end() ? nullptr : it->second;
    }

    void kScriptManager::destroyInstance(const kString &componentUuid)
    {
        auto it = instances.find(componentUuid);
        if (it == instances.end())
            return;

        kScriptInstance *inst = it->second;
        if (inst->module)
            engine->DiscardModule(inst->moduleName.c_str());
        delete inst;
        instances.erase(it);
    }

    void kScriptManager::destroyAllInstances()
    {
        for (auto &pair : instances)
        {
            kScriptInstance *inst = pair.second;
            if (inst->module)
                engine->DiscardModule(inst->moduleName.c_str());
            delete inst;
        }
        instances.clear();
    }

    void kScriptManager::applyGlobalVariable(kScriptInstance *inst,
                                             const kScriptGlobalValue &value)
    {
        if (!inst || !inst->valid || !inst->module || value.name.empty())
            return;

        int idx = inst->module->GetGlobalVarIndexByName(value.name.c_str());
        if (idx < 0)
            return;

        void *addr = inst->module->GetAddressOfGlobalVar(idx);
        if (!addr)
            return;

        if (value.typeName == "int")
            *(int *)addr = value.i;
        else if (value.typeName == "float")
            *(float *)addr = value.f;
        else if (value.typeName == "bool")
            *(bool *)addr = value.b;
        else if (value.typeName == "kVec3")
        {
            float *dst = (float *)addr;
            dst[0] = value.vec[0];
            dst[1] = value.vec[1];
            dst[2] = value.vec[2];
        }
        else if (value.typeName == "kObject@" || value.typeName == "kAnimator@" ||
                 value.typeName == "kAudioSource@" || value.typeName == "kMaterial@")
        {
            // Handles are raw pointers (asOBJ_REF | asOBJ_NOCOUNT): the global
            // storage slot holds a single pointer we can overwrite directly.
            *(void **)addr = value.handle;
        }
        // "string" globals are left untouched (scriptstdstring objects need the
        // engine factory to assign; out of scope for these bindings).
    }

    // -----------------------------------------------------------------------
    // Execution
    // -----------------------------------------------------------------------

    bool kScriptManager::callEvent(kScriptInstance *inst, kScriptEvent evt)
    {
        if (!inst || !inst->valid || evt < 0 || evt >= K_SCRIPT_EVENT_COUNT)
            return false;

        asIScriptFunction *fn = inst->fn[evt];
        if (!fn || !context)
            return false; // the script simply does not define this event

        if (context->Prepare(fn) < 0)
            return false;

        int result = context->Execute();
        if (result != asEXECUTION_FINISHED)
        {
            if (result == asEXECUTION_EXCEPTION)
            {
                printf("Script exception in %s (%s): %s\n",
                       kScriptEventDecl(evt), inst->scriptUuid.c_str(),
                       context->GetExceptionString());
            }
            context->Unprepare();
            return false;
        }

        context->Unprepare();
        return true;
    }

    bool kScriptManager::callEventWithObject(kScriptInstance *inst, kScriptEvent evt,
                                             kObject *other)
    {
        if (!inst || !inst->valid || evt < 0 || evt >= K_SCRIPT_EVENT_COUNT)
            return false;

        asIScriptFunction *fn = inst->fn[evt];
        if (!fn || !context)
            return false; // the script simply does not define this event

        if (context->Prepare(fn) < 0)
            return false;

        // Pass the other object as the function's first argument (a handle
        // parameter, e.g. "kObject@ other"). kObject is registered with
        // asOBJ_REF | asOBJ_NOCOUNT, so a raw pointer is the stored handle.
        context->SetArgObject(0, static_cast<void *>(other));

        int result = context->Execute();
        if (result != asEXECUTION_FINISHED)
        {
            if (result == asEXECUTION_EXCEPTION)
            {
                printf("Script exception in %s (%s): %s\n",
                       kScriptEventDecl(evt), inst->scriptUuid.c_str(),
                       context->GetExceptionString());
            }
            context->Unprepare();
            return false;
        }

        context->Unprepare();
        return true;
    }

    // -----------------------------------------------------------------------
    // Host bindings
    // -----------------------------------------------------------------------

    void kScriptManager::registerGlobalFunction(const kString &declaration, asSFuncPtr func)
    {
        int result = engine->RegisterGlobalFunction(declaration.c_str(), func, asCALL_CDECL);
        assert(result >= 0);
        (void)result;
    }

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------

    void kScriptManager::destroy()
    {
        if (!engine)
            return;

        destroyAllInstances();

        if (context)
        {
            context->Release();
            context = nullptr;
        }

        engine->ShutDownAndRelease();
        engine = nullptr;
    }
}
