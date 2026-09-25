# Scripting

Kemena3D scripts gameplay logic with [AngelScript](https://www.angelcode.com/angelscript/).
A script can be authored two ways — both end up as the same thing:

- **Text** — a hand-written `.as` source file.
- **Visual** — a node graph (`.logic`) edited in the Script Editor, which
  generates an equivalent `.as` file (kept in `Library/GeneratedScripts/` as
  a temp artifact, not shipped).

Either way, the source is compiled to **AngelScript bytecode** and that bytecode
is what runs during play and in an exported game. The editable source is kept
for editing only.

---

## Concepts

| Term | Meaning |
|------|---------|
| Script asset | A `.as` source file, identified by a UUID. |
| Script component | An attachment of a script asset to a `kObject`. |
| Bytecode | Compiled form of a script, stored at `Library/Scripts/<uuid>.kbc`. |
| Instance | A per-object compiled module — each object has independent script globals. |

The pipeline: `source (.as / .logic)` → **compile** → `Library/Scripts/<uuid>.kbc` → **load on Play**.

Compilation happens when a source file is saved (a file watcher recompiles
changed scripts) and via **Run → Build Scripts** in the editor. Play always
loads the freshest bytecode.

---

## Lifecycle functions

A script defines any subset of these free functions. Missing ones are skipped.

| Function | Called |
|----------|--------|
| `void Awake()` | Once, before any `Start()`, when play begins. |
| `void Start()` | Once, on the first frame, after every `Awake()`. |
| `void Update()` | Every frame. |
| `void FixedUpdate()` | Every fixed (physics) step. |
| `void LateUpdate()` | Every frame, after all `Update()` calls. |
| `void OnDestroy()` | Once, when play stops or the object is destroyed. |

`OnEnable()` / `OnDisable()` are reserved for a future release.

Each object that attaches the same script gets its own private module, so
script-global variables are **not** shared between objects.

---

## Host API

The engine exposes the following to every script. For the **complete** reference
— every `kObject`/`kVec3` member, the `string` standard library, number
formatting, and worked examples — see
[**ScriptingAPI.md**](ScriptingAPI.md).

### Globals

```angelscript
kObject@ getSelf();          // the object this script is attached to
float    getDeltaTime();     // seconds since the last frame
float    getFixedDeltaTime();// seconds of the fixed (physics) step
void     print(string);      // log to the console
void     log(string);        // alias of print
```

### `kVec3`

```angelscript
kVec3();                     kVec3(float x, float y, float z);
float x, y, z;
kVec3  opAdd / opSub(const kVec3 &in);
kVec3  opMul(float);         kVec3 opNeg();
bool   opEquals(const kVec3 &in);
float  length();             kVec3 normalized();
float  dot(const kVec3 &in); kVec3 cross(const kVec3 &in);
```

### `kObject`

```angelscript
string  getName() const;            void setName(const string &in);
string  getUuid() const;
kVec3   getPosition() const;        void setPosition(const kVec3 &in);
kVec3   getGlobalPosition() const;
kVec3   getScale() const;           void setScale(const kVec3 &in);
kVec3   getRotation() const;        void setRotation(const kVec3 &in); // Euler degrees
kVec3   forward() const;  kVec3 right() const;  kVec3 up() const;
void    rotate(const kVec3 &in axis, float speed);
void    translate(const kVec3 &in delta);
bool    getActive() const;          void setActive(bool);
kObject@ getParent() const;
```

Rotation is exposed as **Euler angles in degrees** for convenience.

---

## Example

```angelscript
void Start()
{
    print("Hello from " + getSelf().getName());
}

void Update()
{
    // Spin around the Y axis at 45 deg/sec.
    getSelf().rotate(kVec3(0.0f, 1.0f, 0.0f), 45.0f * getDeltaTime());
}
```

See `Kemena3D/sample/asset/sample.as` for the full template.

---

## Visual scripting (node graph)

Open the **Script Editor** panel (Window → Script Editor) to build logic as a
node graph instead of writing text.

- **Right-click** the canvas to add nodes, grouped as Events, Flow, Actions,
  Get, Values and Math/Logic.
- **White wires** are execution flow; **coloured wires** carry data values.
- Drag from one pin to another to connect them; right-click a pin to clear it.
- The **Variables** sidebar declares float variables, emitted as script globals.
- **Save** writes the editable `.logic` to `Assets/` and regenerates the
  AngelScript source to `Library/GeneratedScripts/<logic-uuid>.as` (a temp
  artifact, not shipped), which then flows through the normal bytecode pipeline.

An event node (e.g. *On Update*) becomes a lifecycle function; the exec wire
leaving it sequences the statements.

---

## Using scripts from the engine API

The editor wires everything automatically. To drive scripts yourself (for a
standalone game built directly on the SDK):

```cpp
kWorld *world = createWorld(assetManager);
// ... build the scene; attach scripts to objects ...

world->startScripts();          // compile/load + Awake() + Start()

while (running)
{
    world->fixedUpdateScripts(dt);   // FixedUpdate()
    world->updateScripts(dt);        // Update() + LateUpdate()
    renderer->render(scene, /* ... */);
}

world->stopScripts();           // OnDestroy() + release instances
```

`kWorld` owns a `kScriptManager` (see `kscriptmanager.h`) which handles the
script-asset registry, text/bytecode compilation, and per-object instances.
Host bindings are registered automatically when the world is created.
