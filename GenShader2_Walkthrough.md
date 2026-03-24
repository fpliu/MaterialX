# MaterialX Shader Generation: Before and After

## The Big Picture — What Does MaterialX Shader Generation Do?

MaterialX is a standard for describing materials and looks in computer graphics. A "material" is a description of how a surface looks — its color, roughness, metallic-ness, transparency, etc. But GPUs don't understand materials; they understand **shader programs** (GLSL, MSL, MDL, OSL — language-specific code that runs on the GPU).

**Shader generation** is the bridge: you give it a material description (a graph of connected nodes), and it produces actual shader source code you can compile and run.

---

## Before: How the Original System Works

The original system has three layers:

1. **`ShaderGraph`** — an internal DAG (directed acyclic graph) of shader nodes, with inputs wired to outputs. This is the "intermediate representation" that the code generators consume.

2. **`ShaderGenerator`** (and subclasses like `GlslShaderGenerator`) — takes a `ShaderGraph` and emits the actual source code text (GLSL, MDL, etc.).

3. **`ShaderGraph::create()`** — the factory method that *builds* the `ShaderGraph` by walking a MaterialX `Element` tree.

The flow looks like this:

```
MaterialX Document (XML)
    |
    v
Element Hierarchy (C++ objects: Document, Node, NodeGraph, Output, Input...)
    |
    v  ShaderGraph::create()  <-- walks Element tree directly
    |
ShaderGraph (internal DAG)
    |
    v  GlslShaderGenerator::generate()
    |
GLSL source code (text)
```

**The key thing to notice:** `ShaderGraph::create()` is *tightly coupled* to the MaterialX Element hierarchy. It literally calls methods like `node->getInputs()`, `element->getType()`, `doc->getNodeDef(...)` — it navigates the MaterialX C++ object model directly.

This works perfectly when your material data *is* a MaterialX document. You parse some XML, you get MaterialX objects, you generate shaders. Done.

---

## The Problem

Other systems also need to generate MaterialX shaders, but **their material data doesn't live in MaterialX objects**.

The most important example is **USD/Hydra** (Pixar's scene description framework used in film/VFX). Hydra has its own material graph type called `HdMaterialNetwork2`. When Hydra wants to render a MaterialX material, the current workflow is:

```
HdMaterialNetwork2 (Hydra's native format)
    |
    v  Convert everything into MaterialX objects (expensive round-trip!)
    |
MaterialX Element Hierarchy
    |
    v  ShaderGraph::create()
    |
ShaderGraph --> GLSL code
```

That middle step — constructing a full MaterialX Element hierarchy just to throw it away after graph building — is **wasteful, fragile, and architecturally awkward**. You're building a complex object tree solely because the graph builder demands it as input, not because you actually need it.

---

## After: The New Approach (GenShader2)

The new system introduces an **abstraction layer** between "where your material data lives" and "how the shader graph gets built." It has four key pieces:

### 1. `DataHandle` — An Opaque ID

Instead of passing around MaterialX `Element*` pointers, the new system uses a plain `uint64_t` number. The graph builder never looks inside it — it just passes handles back to the data source to ask questions. This is what makes the system truly generic.

Implementations can store whatever they want in those 64 bits: a raw pointer, an array index, a hash, a packed pair of values. The builder doesn't care.

**Lifetime contract:** handles are valid only for the lifetime of the `IShaderSource` that issued them. They must not be shared across `IShaderSource` instances. `InvalidHandle` (0) is the sentinel for "absent / not found."

### 2. `IShaderSource` — The Query Interface

This is the core innovation. It's a pure virtual class (~43 methods) that asks questions like:

- "What's the root element of this graph?"
- "Is this handle a Node or an Output?"
- "What inputs does this node have?"
- "What's connected to this input?"
- "What's the type/value/colorspace of this port?"

The graph builder **pulls** data through this interface instead of **pushing** through MaterialX objects. Anyone who can answer these questions can drive shader generation.

### 3. `MxElementAdapter` — The Compatibility Layer

This is the reference implementation of `IShaderSource` that wraps a traditional MaterialX `Document + Element` pair. It answers all those `IShaderSource` questions by delegating to the existing MaterialX API. This means the old workflow still works exactly as before, just routed through the new interface.

### 4. `ShaderGraphBuilder` — The Decoupled Graph Walker

Replaces the graph traversal logic that was baked into `ShaderGraph::create()`. It does a BFS (breadth-first search) through the material graph by asking the `IShaderSource` questions, and produces a `ShaderGraph2` (which is just a `ShaderGraph` subclass with some extra construction hooks exposed).

---

## The New Flow

**With MaterialX data (same result, new path):**

```
MaterialX Document
    |
    v  MxElementAdapter (implements IShaderSource)
    |
IShaderSource interface
    |
    v  ShaderGraphBuilder (BFS traversal)
    |
ShaderGraph2 (same ShaderGraph, built differently)
    |
    v  GlslShaderGenerator::generate(ShaderGraphPtr)
    |
GLSL source code
```

**With USD/Hydra data (the new capability):**

```
HdMaterialNetwork2 (Hydra's native format)
    |
    v  HdMtlxShaderSource (implements IShaderSource)  <-- NO conversion step!
    |
IShaderSource interface
    |
    v  ShaderGraphBuilder (BFS traversal)
    |
ShaderGraph2
    |
    v  GlslShaderGenerator::generate(ShaderGraphPtr)
    |
GLSL source code
```

The expensive round-trip is gone. Hydra just implements `IShaderSource` to answer questions about its own `HdMaterialNetwork2` objects directly.

---

## Changes to Existing Files

This PR adds `generate(name, ShaderGraphPtr, context)` overloads to all built-in generators so that a pre-built graph can be emitted directly. All changes are **additive** — no existing method signatures or behaviors were modified.

### New overloads on generators

| File | Change |
|------|--------|
| `ShaderGenerator.h/.cpp` | New virtual `generate(name, ShaderGraphPtr, context)` — throws by default; each backend opts in by overriding. |
| `GlslShaderGenerator.h/.cpp` | Override of `generate(ShaderGraphPtr)` — delegates to new `HwShaderGenerator::createShader(ShaderGraphPtr)`, then runs the existing emit path. (+24 lines) |
| `MslShaderGenerator.h/.cpp` | Same pattern as GLSL. (+31 lines) |
| `MdlShaderGenerator.h/.cpp` | Override of `generate(ShaderGraphPtr)` + private `createShader(ShaderGraphPtr)` — sets up MDL shader stages from pre-built graph. (+223 lines) |
| `OslShaderGenerator.h/.cpp` | Override of `generate(ShaderGraphPtr)` + private `createShader(ShaderGraphPtr)` — sets up OSL stage from pre-built graph. Note: `oslConnectCiWrapper` is not supported via this path. (+156 lines) |
| `HwShaderGenerator.h/.cpp` | New `createShader(name, ShaderGraphPtr, context)` overload — sets up HW shader stages and uniforms from a pre-built graph instead of from an Element tree. Shared by GLSL and MSL. (+177 lines) |

### Minimal changes to base classes

| File | Change |
|------|--------|
| `ShaderGraph.h` | One new public accessor: `getDocument()` returns the `_document` member. (3 lines) |

### What was NOT changed

- **Emitters are untouched.** The code that writes GLSL/MDL/OSL/MSL text was not modified. All complexity is in graph construction, not code emission.
- **No existing method signatures changed.** All modifications are new overloads or new accessors.
- **No changes to `ShaderGraph::create()`.** The original graph-building path is fully preserved.

---

## The `GenContextCreate` Orchestrator

This is the user-facing entry point that ties it all together:

```cpp
auto adapter = std::make_unique<MxElementAdapter>(doc, element);
GenContextCreate ctx(GlslShaderGenerator::create(), std::move(adapter));
ctx.getGenContext().registerSourceCodeSearchPath(searchPath);

ShaderGraph2Ptr graph = ctx.buildGraph("myShader");
ShaderPtr shader = ctx.buildShader("myShader");  // graph + emit in one call
```

---

## MX Bridge Dependency Matrix

`IShaderSource` includes four optional "MX bridge" methods. These return live MaterialX types for codegen paths that have not yet been fully abstracted. Here is the current status:

| Bridge method | Required by `buildGraph()`? | Required by `buildShader()`? | Can non-MX backends return nullptr? |
|---|---|---|---|
| `getMxNodeDef(DataHandle)` | **Yes** — needed for every node (root + upstream) to resolve implementation. NodeDefs live in the loaded library, so any backend that can drive generation will have them available. | Yes | No — must be implemented. |
| `getMxNodeDefByHandle(DataHandle)` | **Yes** — needed by `addDefaultGeomNode2()` to construct geomprop shader nodes. | Yes | No — must be implemented alongside `getNodeDefByName()`. |
| `getMxDocument()` | **No** — fully eliminated from graph construction (Phase 4c). | Yes — `buildShader()` calls `setDocument()` on the graph for HW geomprop insertion. | Partially — can return nullptr if geomprops are pre-resolved. |
| `getMxNode(DataHandle)` | **No** — fully eliminated (Phase 4). | No | Yes — safe to leave as nullptr. |

---

## Known Limitations

1. **BFS vs. DFS traversal order.** `ShaderGraphBuilder` uses BFS; the original `ShaderGraph::create()` uses DFS. For materials with complex NodeGraph structures, this can produce **non-semantic differences** in emitted shader code (e.g., different variable declaration order). The generated shaders are functionally equivalent. The test suite uses a relaxed parity check for the full examples sweep, and strict byte-for-byte checks for a representative set of materials.

2. **`ShaderNodeImpl::setValues(const Node&, ...)` is skipped.** This virtual method requires a `ConstNodePtr` and is not called for upstream nodes via the new path. The only known override is `HwImageNode::setValues` (UDIM UV normalization). That path must still use the original `createNode(ConstNodePtr)` workflow or call `getMxNode()`.

3. **OSL `mtlx_category` annotation.** The pre-built `ShaderGraphPtr` path cannot recover the node category from the graph alone (no `ElementPtr` available), so `mtlx_category` is always `""`. The parity tests normalize this before comparing.

4. **OSL `oslConnectCiWrapper`.** This option is not supported via `generate(ShaderGraphPtr)` because it requires a Document for NodeDef lookup. Use `generate(ElementPtr)` directly when this option is needed.

5. **NodeGraph output-to-output cycles.** If a NodeGraph contains output-to-output connections that form a cycle in the BFS, the builder detects and skips them with a `WARN`. This is a rare edge case in practice.

---

## Why This Matters (The Value)

| Before | After |
|--------|-------|
| Graph building was welded to MaterialX objects | Graph building works through a generic interface |
| USD/Hydra had to construct & destroy a full MaterialX Element tree just to generate shaders | USD/Hydra implements `IShaderSource` over its own native types — no conversion |
| Adding a new data source meant forking or monkey-patching `ShaderGraph::create()` | Adding a new data source means writing one class that implements `IShaderSource` |
| Graph construction and code emission were one monolithic step | Graph construction and code emission are cleanly separable — you can inspect/cache/reuse the graph |
| Testing graph building required real MaterialX documents | Testing can use stub/mock `IShaderSource` implementations |

---

## Key Design Decisions

1. **Pull, not push.** The builder asks the data source for what it needs (pull), rather than the data source pushing a pre-formatted structure. This means the data source only needs to expose what it already knows — no up-front conversion.

2. **Opaque handles, not pointers.** `DataHandle` being a plain `uint64_t` means the interface makes zero assumptions about how the backend stores its data.

3. **Subclass, don't rewrite.** `ShaderGraph2` extends `ShaderGraph` rather than replacing it. The downstream code generators don't need to change at all — they still consume a `ShaderGraph`.

4. **Bridge methods for pragmatism.** `IShaderSource` includes optional "MX bridge" methods (`getMxNodeDef`, `getMxDocument`) because some deep codegen paths still need live MaterialX types (e.g., NodeDef lookup for implementation resolution). These are documented escape hatches, not permanent requirements.

---

## Implementing a New `IShaderSource` Backend

To drive shader generation from a non-MaterialX data source, implement a class that inherits from `IShaderSource`. Here is a practical guide:

### Minimum viable implementation

All ~43 pure virtual methods must be implemented (the compiler enforces this), but many can return `InvalidHandle` or empty strings for features your data source doesn't use (e.g., `getPortUnit()` if your system has no unit metadata).

The **critical subset** that must return real data:

| Method group | Why it matters |
|---|---|
| `getRootElement()`, `isNode()`, `isOutput()` | The builder needs to know what it's starting from. |
| `getElementName()`, `getElementPath()` | Used as keys for node de-duplication and naming. |
| `getNodeInputCount/Input()`, `getInputConnectedNode()` | Core graph topology — how nodes are wired together. |
| `getNodeDef()`, `getNodeDefByName()`, `getNodeDefType()` | NodeDef resolution — the builder must find an implementation for every node. |
| `getNodeDefInputCount/Input/OutputCount/Output()` | Socket creation — the builder reads the NodeDef interface to set up ShaderNode ports. |
| `getPortName()`, `getPortType()`, `getPortValueString()` | Every input needs a name, type, and optionally a default value. |
| `getMxNodeDef()`, `getMxNodeDefByHandle()` | Bridge methods — must return valid NodeDefs from the loaded library. |

### Optional / safe to stub

- `getPortColorSpace()`, `getPortActiveColorSpace()` — return `""` if your system doesn't track color spaces.
- `getPortUnit()`, `getPortUnitType()`, `getPortActiveUnit()` — return `""` if your system has no unit metadata.
- `portHasDefaultGeomProp()`, `getPortDefaultGeomProp()`, and the `GeomPropDef` methods — return false / `InvalidHandle` / `""` if you pre-resolve geometric properties.
- `getMxDocument()` — return `nullptr` if you don't need HW geomprop insertion during `buildShader()`.
- `getMxNode()` — return `nullptr` (no longer called by the builder).

### Error behavior

When the builder encounters an unexpected `InvalidHandle` (e.g., `getNodeDef()` returns `InvalidHandle` for a node), it throws `ExceptionShaderGenError` with a descriptive message identifying the node name and the failed query. This makes debugging a new backend straightforward.

### Reference

See `MxElementAdapter` (wraps MaterialX objects) as the canonical example, and the `HdMtlxShaderSource` implementation in the OpenUSD fork for a real non-MX backend.

---

## Test Coverage

The test suite lives in `source/MaterialXTest/MaterialXGenShader2/GenShader2Parity.cpp` (~1,090 lines). It has five layers:

### 1. Adapter unit tests (`[genshader2][adapter]`)

Verify that `MxElementAdapter` faithfully reflects the MaterialX data model through the `IShaderSource` interface — root element identity, node input enumeration, NodeDef lookup, document-level queries.

### 2. Structural graph parity (`[genshader2][parity]`)

Build a `ShaderGraph` via both the old (`ShaderGraph::create()`) and new (`GenContextCreate::buildGraph()`) paths, then assert structural equivalence: same node count, same node names, same input/output counts, same connections, and same literal values. Tested across 7 materials:

- `standard_surface_default`, `standard_surface_marble_solid`, `standard_surface_glass`
- `open_pbr_default`, `open_pbr_carpaint`
- `gltf_pbr_default`
- `standard_surface_brass_tiled` (output-rooted NodeGraph)

### 3. Shader emit parity (`[genshader2][emit]`)

Drive full code emission through `GenContextCreate::buildShader()` and assert byte-for-byte identical output vs. the original `ShaderGenerator::generate(ElementPtr)` path. Covers:

- **GLSL**: `standard_surface_default`, `standard_surface_marble_solid`, `open_pbr_default`
- **MDL**: `standard_surface_default`, `open_pbr_default`
- **OSL**: `standard_surface_default`, `open_pbr_default` (with `mtlx_category` normalization)
- **MSL**: `standard_surface_default`, `open_pbr_default`

### 4. Bridge invariant tests (`[genshader2][phase4]`, `[phase4b]`, `[phase4c]`)

Prove that specific MX bridge methods are no longer called during graph construction:

- **`NoMxNodeAdapter`**: subclass that `FAIL()`s if `getMxNode()` is called. Tested across 3 materials.
- **`CountingDocumentAdapter`**: subclass that counts `getMxDocument()` calls and asserts the count is **zero** after `buildGraph()`. Tested for both node-root and output-root graphs.

### 5. Full examples sweep (`[genshader2][emit][sweep]`)

Iterates over all `.mtlx` files under `resources/Materials/Examples/`, generates GLSL shaders via both paths, and verifies the new path succeeds without crashing. Uses relaxed parity (logs ordering differences as warnings rather than failures).

### Running the tests

```bash
# Build with CMake (GenShader2 is always included)
cmake --build build --target MaterialXTest

# Run all GenShader2 tests
./build/bin/MaterialXTest "[genshader2]"

# Run only parity tests
./build/bin/MaterialXTest "[parity]"

# Run only emit tests
./build/bin/MaterialXTest "[emit]"

# Run only the sweep
./build/bin/MaterialXTest "[sweep]"

# Run Phase 4 bridge invariant tests
./build/bin/MaterialXTest "[phase4]"
```

---

## Build Integration

`MaterialXGenShader2` is a new CMake library target, always built (no feature flag to disable it).

- **Target**: `MaterialXGenShader2`
- **Dependencies**: `MaterialXGenShader`, `MaterialXFormat`, `MaterialXCore`
- **Export macro**: `MATERIALX_GENSHADER2_EXPORTS` (standard MaterialX DLL export pattern via `Export.h`)
- **Root CMakeLists.txt**: one line added — `add_subdirectory(source/MaterialXGenShader2)`, placed right after `add_subdirectory(source/MaterialXGenShader)`.
- **Test integration**: `source/MaterialXTest/CMakeLists.txt` adds the test subdirectory and links `MaterialXGenShader2`. Tests compile conditionally against available generators (`MATERIALX_BUILD_GEN_MDL`, `MATERIALX_BUILD_GEN_OSL`, `MATERIALX_BUILD_GEN_MSL`).

---

## Library Structure

The new code lives in `source/MaterialXGenShader2/` — a flat library of 7 headers and 4 source files (~3,000 lines total):

| File | Purpose |
|------|---------|
| `DataHandle.h` | Opaque `uint64_t` handle type + `InvalidHandle` sentinel |
| `IShaderSource.h` | Pure virtual query interface (~43 methods) |
| `MxElementAdapter.h/.cpp` | Reference `IShaderSource` implementation over MaterialX objects |
| `ShaderGraph2.h/.cpp` | `ShaderGraph` subclass exposing construction hooks |
| `ShaderGraphBuilder.h/.cpp` | BFS graph traversal driven by `IShaderSource` |
| `GenContextCreate.h/.cpp` | Orchestrator: owns source + context, exposes `buildGraph()` / `buildShader()` |
| `Export.h` / `Library.h` | Standard MaterialX DLL export macros and library registration |

Tests live in `source/MaterialXTest/MaterialXGenShader2/GenShader2Parity.cpp` (~1,090 lines, 20+ test cases).
