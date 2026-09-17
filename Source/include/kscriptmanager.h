/**
 * @file kscriptmanager.h
 * @brief AngelScript engine wrapper: script-asset registry, text/bytecode
 *        compilation, and per-object script instances.
 *
 * A *script asset* is a UUID-keyed source file (a text @c .as file, or the
 * @c .as text generated from a node graph). The manager can compile a script
 * asset to AngelScript bytecode (@c SaveByteCode) and, at runtime, build a
 * private @c asIScriptModule for each object that attaches the script —
 * preferring the precompiled bytecode and falling back to the source text.
 *
 * Each object gets its own module instance so that script-global variables
 * are independent per object (the lifecycle-function script style used by
 * the editor's @c sample.as template).
 */

#ifndef KSCRIPTMANAGER_H
#define KSCRIPTMANAGER_H

#include "kexport.h"

#include <cassert>
#include <vector>
#include <map>
#include <cstdio>
#include <iostream>
#include <functional>

#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>
#include <scriptbuilder/scriptbuilder.h>

#include "kdatatype.h"

#define FUNCTION asFUNCTION

namespace kemena
{
    class kObject;
    class kInputManager;
    class kAudioManager;
    class kPhysicsManager;

    /**
     * @brief How a script asset's source is authored.
     */
    enum kScriptSourceType
    {
        K_SCRIPT_TEXT      = 0, ///< Hand-written AngelScript text (.as).
        K_SCRIPT_NODEGRAPH = 1, ///< Visual node graph; sourcePath points at its generated .as.
    };

    /**
     * @brief Lifecycle events dispatched to every active script each frame.
     *
     * The integer order matters: K_SCRIPT_EVENT_COUNT must stay last so it can
     * size per-instance arrays.
     */
    enum kScriptEvent
    {
        K_SCRIPT_AWAKE = 0,   ///< void Awake()       — once, before any Start().
        K_SCRIPT_START,       ///< void Start()       — once, on the first frame.
        K_SCRIPT_UPDATE,      ///< void Update()      — every frame.
        K_SCRIPT_FIXED_UPDATE,///< void FixedUpdate() — every fixed (physics) step.
        K_SCRIPT_LATE_UPDATE, ///< void LateUpdate()  — every frame, after Update().
        K_SCRIPT_ON_ENABLE,   ///< void OnEnable()    — when the component activates.
        K_SCRIPT_ON_DISABLE,  ///< void OnDisable()   — when the component deactivates.
        K_SCRIPT_ON_DESTROY,  ///< void OnDestroy()   — when the instance is torn down.

        // Physics contact / trigger events (dispatched by the physics system).
        K_SCRIPT_ON_COLLISION_ENTER, ///< void OnCollisionEnter() — rigid-body contact started.
        K_SCRIPT_ON_COLLISION_STAY,  ///< void OnCollisionStay()  — rigid-body contact persists.
        K_SCRIPT_ON_COLLISION_EXIT,  ///< void OnCollisionExit()  — rigid-body contact ended.
        K_SCRIPT_ON_TRIGGER_ENTER,   ///< void OnTriggerEnter()   — sensor overlap started.
        K_SCRIPT_ON_TRIGGER_STAY,    ///< void OnTriggerStay()    — sensor overlap persists.
        K_SCRIPT_ON_TRIGGER_EXIT,    ///< void OnTriggerExit()    — sensor overlap ended.

        K_SCRIPT_EVENT_COUNT  ///< Sentinel — number of lifecycle events.
    };

    /// AngelScript declaration string for each kScriptEvent (indexed by enum).
    KEMENA3D_API const char *kScriptEventDecl(kScriptEvent evt);

    /**
     * @brief Per-object script *component* descriptor.
     *
     * This is the lightweight, serialisable record stored on a kObject. It only
     * references a script *asset* by UUID — the live compiled module is owned by
     * kScriptManager and looked up via @c uuid (the component UUID).
     */
    /**
     * @brief User-assigned value for one script-scope global variable.
     *
     * The inspector enumerates the globals declared by a script asset (from its
     * source text) and lets the user assign values or object references. These
     * bindings are stored on the kScript component and applied to the private
     * module's globals when the instance is created at runtime.
     */
    struct KEMENA3D_API kScriptVarBinding
    {
        kString name;                 ///< Global variable name.
        kString typeName;             ///< AngelScript type name (e.g. "kAnimator@").
        kString valueStr;             ///< String value, or referenced object UUID for handles.
        float   valueFloat[3] = {0.0f, 0.0f, 0.0f}; ///< int/float/bool/vec3 storage.
        bool    valueBool = false;    ///< Bool storage.
        bool    assigned  = false;    ///< True once the user set a value in the inspector.
    };

    /**
     * @brief Per-object script *component* descriptor.
     *
     * This is the lightweight, serialisable record stored on a kObject. It only
     * references a script *asset* by UUID — the live compiled module is owned by
     * kScriptManager and looked up via @c uuid (the component UUID).
     */
    struct KEMENA3D_API kScript
    {
        kString uuid;              ///< UUID of this attachment (unique per object+slot).
        kString scriptUuid;        ///< UUID of the referenced script asset.
        kString fileName;          ///< Source file path (editor convenience / fallback).
        kString checksum;          ///< Source checksum recorded at last compile.
        bool    isActive = true;   ///< Whether this script runs each frame.

        ///< User-assigned values for script-scope globals (editor inspector).
        std::vector<kScriptVarBinding> variableBindings;
    };

    /**
     * @brief A registered script asset: a compilable unit of script source.
     */
    struct KEMENA3D_API kScriptAsset
    {
        kString           uuid;                       ///< Script asset UUID.
        kString           sourcePath;                 ///< Path to the .as source.
        kString           bytecodePath;               ///< Path to compiled bytecode, or empty.
        kScriptSourceType sourceType = K_SCRIPT_TEXT;  ///< Text vs node-graph origin.
        kString           checksum;                   ///< Source checksum at last compile.
    };

    /**
     * @brief A live, per-object compiled script module.
     *
     * Created by kScriptManager::createInstance(). Holds a private module so
     * script globals are not shared between objects, plus cached lifecycle
     * function handles so per-frame dispatch needs no string lookups.
     */
    struct KEMENA3D_API kScriptInstance
    {
        kString            componentUuid;          ///< Matches the owning kScript::uuid.
        kString            scriptUuid;             ///< Script asset this was built from.
        kString            moduleName;             ///< Private AngelScript module name.
        asIScriptModule   *module = nullptr;       ///< Private compiled module.
        asIScriptFunction *fn[K_SCRIPT_EVENT_COUNT] = {}; ///< Cached lifecycle handles.
        kObject           *owner = nullptr;        ///< Object this instance belongs to.
        bool               awakeCalled = false;    ///< Awake() has run.
        bool               startCalled = false;    ///< Start() has run.
        bool               valid = false;          ///< Module compiled/loaded successfully.
    };

    /**
     * @brief Minimal file-backed @c asIBinaryStream for bytecode I/O.
     *
     * Used by kScriptManager to write (@c SaveByteCode) and read
     * (@c LoadByteCode) AngelScript bytecode to and from disk.
     */
    class KEMENA3D_API kFileByteStream : public asIBinaryStream
    {
    public:
        /**
         * @brief Opens @p path for bytecode reading or writing.
         * @param path    File path to open.
         * @param writing true to open for writing, false for reading.
         */
        kFileByteStream(const kString &path, bool writing);

        /** @brief Closes the underlying file if it is open. */
        ~kFileByteStream() override;

        /** @brief Returns true if the underlying file opened successfully. */
        bool isOpen() const { return file != nullptr; }

        /**
         * @brief Reads @p size bytes from the file into @p ptr.
         * @param ptr  Destination buffer.
         * @param size Number of bytes to read.
         * @return 0 on success, negative on error (asIBinaryStream convention).
         */
        int Read(void *ptr, asUINT size) override;

        /**
         * @brief Writes @p size bytes from @p ptr to the file.
         * @param ptr  Source buffer.
         * @param size Number of bytes to write.
         * @return 0 on success, negative on error (asIBinaryStream convention).
         */
        int Write(const void *ptr, asUINT size) override;

    private:
        std::FILE *file = nullptr;
    };

    /**
     * @brief Manages the AngelScript engine, a registry of script assets, and
     *        the live per-object script module instances.
     *
     * Typical engine-side flow:
     * @code
     *   kScriptManager mgr;
     *   mgr.registerScriptAsset("uuid-1", "Assets/player.as");
     *   mgr.compileToBytecode("uuid-1", "Library/Scripts/uuid-1.kbc");
     *   kScriptInstance *inst = mgr.createInstance("comp-1", "uuid-1");
     *   mgr.setActiveObject(playerObject);
     *   mgr.callEvent(inst, K_SCRIPT_START);
     * @endcode
     */
    class KEMENA3D_API kScriptManager
    {
    public:
        /** @brief Creates and initialises the AngelScript engine and shared context. */
        kScriptManager();

        /** @brief Destroys all instances and shuts down the engine (see destroy()). */
        virtual ~kScriptManager();

        /** @brief Returns the underlying AngelScript engine (for binding setup). */
        asIScriptEngine *getEngine() { return engine; }

        /** @brief Severity of a script compiler message. */
        enum MessageSeverity { MSG_ERROR = 0, MSG_WARNING = 1, MSG_INFO = 2 };

        /**
         * @brief Sink for AngelScript compiler messages.
         * @param severity One of MessageSeverity.
         * @param section  Source section (file/module) the message refers to.
         * @param row,col  Location in the section.
         * @param message  Human-readable compiler message.
         */
        using MessageHandler = std::function<void(int severity, const char *section,
                                                  int row, int col, const char *message)>;

        /**
         * @brief Sink for runtime script output (print / log / printConsole).
         */
        using ScriptPrintHandler = std::function<void(const char *message)>;

        /**
         * @brief Installs a process-wide handler for compiler messages.
         *
         * When set, every script compile error/warning/info is forwarded here
         * (in addition to stdout) so a host (e.g. the editor) can surface them.
         * Pass an empty handler to revert to stdout-only.
         */
        static void setMessageHandler(MessageHandler handler);

        /**
         * @brief Installs a process-wide handler for runtime script print output.
         *
         * When set, script `print`/`log`/`printConsole` calls are forwarded here
         * (e.g. to the editor Console panel). Pass an empty handler to revert to
         * stdout-only behaviour.
         */
        static void setScriptPrintHandler(ScriptPrintHandler handler);

        /**
         * @brief Dispatches a runtime script print message to the handler or stdout.
         * @param message NUL-terminated message text.
         */
        static void handleScriptPrint(const char *message);

        // --- Script-asset registry ------------------------------------------

        /**
         * @brief Registers (or updates) a script asset.
         * @param uuid         Script asset UUID.
         * @param sourcePath   Path to the .as source file.
         * @param type         Text or node-graph origin.
         * @param bytecodePath Optional path to precompiled bytecode.
         */
        void registerScriptAsset(const kString &uuid, const kString &sourcePath,
                                 kScriptSourceType type = K_SCRIPT_TEXT,
                                 const kString &bytecodePath = "");

        /** @brief Returns the asset for @p uuid, or nullptr if unregistered. */
        kScriptAsset *getScriptAsset(const kString &uuid);

        /** @brief Removes a script asset from the registry. */
        void unregisterScriptAsset(const kString &uuid);

        /** @brief Records the bytecode path for an already-registered asset. */
        void setBytecodePath(const kString &uuid, const kString &bytecodePath);

        // --- Compilation -----------------------------------------------------

        /**
         * @brief Compiles a registered asset's source to AngelScript bytecode.
         * @param scriptUuid Asset UUID (must be registered).
         * @param outPath    Destination bytecode file.
         * @param stripDebug Strip debug info for a smaller/faster build.
         * @return true on success; updates the asset's bytecodePath and checksum.
         */
        bool compileToBytecode(const kString &scriptUuid, const kString &outPath,
                               bool stripDebug = false);

        /**
         * @brief Compiles a source file to bytecode without touching the registry.
         * @param sourcePath Path to the .as source.
         * @param outPath    Destination bytecode file.
         * @param stripDebug Strip debug info.
         * @return true on success.
         */
        bool compileFileToBytecode(const kString &sourcePath, const kString &outPath,
                                   bool stripDebug = false);

        // --- Per-object instances -------------------------------------------

        /**
         * @brief Builds a private module instance for one object's script slot.
         *
         * Prefers loading the asset's bytecode; if absent or stale, compiles the
         * source text directly. Safe to call again with the same componentUuid —
         * the previous instance is destroyed first.
         *
         * @param componentUuid The owning kScript::uuid.
         * @param scriptUuid    The script asset to instantiate.
         * @return Instance pointer (owned by the manager), or nullptr on failure.
         */
        kScriptInstance *createInstance(const kString &componentUuid,
                                        const kString &scriptUuid);

        /** @brief Returns the instance for @p componentUuid, or nullptr. */
        kScriptInstance *getInstance(const kString &componentUuid);

        /** @brief Destroys one instance and discards its module. */
        void destroyInstance(const kString &componentUuid);

        /** @brief Destroys every live instance (e.g. when Play stops). */
        void destroyAllInstances();

        /**
         * @brief Value to write into one script-global variable of a live instance.
         *
         * Primitive types use @c i / @c f / @c b / @c vec; handle types
         * ("kObject@", "kAnimator@", ...) use @c handle.
         */
        struct KEMENA3D_API kScriptGlobalValue
        {
            kString name;
            kString typeName;
            int    i      = 0;
            float  f      = 0.0f;
            bool   b      = false;
            float  vec[3] = {0.0f, 0.0f, 0.0f};
            void  *handle = nullptr;
        };

        /**
         * @brief Writes a value into one global variable of a live instance.
         *
         * Looks the global up by name on the instance's private module and writes
         * the value at its storage address. Handles are stored as raw pointers
         * (kObject/kAnimator/... are registered with asOBJ_REF | asOBJ_NOCOUNT).
         *
         * @param inst  Instance whose module globals are written.
         * @param value Name, type and value to apply.
         */
        void applyGlobalVariable(kScriptInstance *inst, const kScriptGlobalValue &value);

        // --- Execution -------------------------------------------------------

        /**
         * @brief Sets the object scripts see as their owner during dispatch.
         *
         * Call immediately before callEvent() so bound host functions (e.g.
         * @c getSelf()) resolve to the correct object.
         */
        void setActiveObject(kObject *obj) { activeObject = obj; }

        /** @brief Returns the object set by setActiveObject(). */
        kObject *getActiveObject() const { return activeObject; }

        /** @brief Sets the frame delta time exposed to scripts via host bindings. */
        void setDeltaTime(float dt) { deltaTime = dt; }

        /** @brief Returns the current frame delta time. */
        float getDeltaTime() const { return deltaTime; }

        /** @brief Sets the fixed (physics) delta time exposed to scripts. */
        void setFixedDeltaTime(float dt) { fixedDeltaTime = dt; }

        /** @brief Returns the current fixed delta time. */
        float getFixedDeltaTime() const { return fixedDeltaTime; }

        /**
         * @brief Attaches the named input manager scripts use for getAction()/getAxis().
         * @param manager Input manager instance; may be null to disable input queries.
         */
        void setInputManager(kInputManager *manager) { inputManager = manager; }

        /** @brief Returns the attached named input manager, or nullptr. */
        kInputManager *getInputManager() const { return inputManager; }

        /**
         * @brief Attaches the audio manager scripts use for playback and listener control.
         * @param manager Audio manager instance; may be null to disable audio calls.
         */
        void setAudioManager(kAudioManager *manager) { audioManager = manager; }

        /** @brief Returns the attached audio manager, or nullptr. */
        kAudioManager *getAudioManager() const { return audioManager; }

        /**
         * @brief Attaches the physics manager scripts use for global physics queries.
         * @param manager Physics manager instance; may be null to disable those calls.
         */
        void setPhysicsManager(kPhysicsManager *manager) { physicsManager = manager; }

        /** @brief Returns the attached physics manager, or nullptr. */
        kPhysicsManager *getPhysicsManager() const { return physicsManager; }

        /**
         * @brief Invokes one lifecycle function on an instance, if it defines it.
         * @param inst Instance to dispatch to.
         * @param evt  Lifecycle event.
         * @return true if the function existed and finished without exception.
         */
        bool callEvent(kScriptInstance *inst, kScriptEvent evt);

        /**
         * @brief Runs a collision/trigger event, passing the other object.
         *
         * Collision/trigger event functions are generated as
         * @c void OnCollisionEnter(kObject@ other) / @c void OnTriggerEnter(kObject@ other)
         * etc.; @p other is bound as the first (and only) argument.
         *
         * @param inst  Instance whose cached event function runs (no-op when absent).
         * @param evt   Collision/trigger event to dispatch.
         * @param other The other object involved in the contact/overlap (may be null).
         * @return true when the event function existed and finished cleanly.
         */
        bool callEventWithObject(kScriptInstance *inst, kScriptEvent evt, kObject *other);

        // --- Host bindings ---------------------------------------------------

        /**
         * @brief Registers a native C function callable from every script.
         * @param declaration AngelScript declaration string.
         * @param func        Native function pointer (use the FUNCTION macro).
         */
        void registerGlobalFunction(const kString &declaration, asSFuncPtr func);

        /** @brief Shuts down the engine and releases all resources. */
        void destroy();

    protected:
    private:
        /// Loads/compiles a module for @p inst from @p asset; fills cached handles.
        bool buildInstanceModule(kScriptInstance *inst, const kScriptAsset &asset);
        /// Caches the lifecycle asIScriptFunction handles on @p inst.
        void cacheInstanceFunctions(kScriptInstance *inst);

        asIScriptEngine  *engine         = nullptr; ///< Shared AngelScript engine.
        asIScriptContext *context        = nullptr; ///< Reused execution context.
        kObject          *activeObject   = nullptr; ///< Object visible to running scripts.
        unsigned int      moduleCounter  = 0;       ///< Ensures unique module names.
        float             deltaTime      = 0.0f;    ///< Per-frame delta time for scripts.
        float             fixedDeltaTime = 0.0f;    ///< Fixed-step delta time for scripts.
        kInputManager    *inputManager   = nullptr; ///< Named input manager for script input queries.
        kAudioManager    *audioManager   = nullptr; ///< Audio manager for script playback calls.
        kPhysicsManager  *physicsManager = nullptr; ///< Physics manager for script gravity/raycast calls.

        std::map<kString, kScriptAsset>     assets;    ///< Script assets by UUID.
        std::map<kString, kScriptInstance*> instances; ///< Live instances by component UUID.
    };
}

#endif // KSCRIPTMANAGER_H
