 Project Summary — MaterialXGenShader2 (Issue #2566)

  Starting point

  You asked me to look at https://github.com/AcademySoftwareFoundation/MaterialX/issues/2566, which proposed decoupling shader graph construction from the MaterialX Element
  hierarchy. The motivation: systems like USD/Hydra have their own material graph representations (HdMaterialNetwork2) and currently have to construct a full MaterialX Element
  hierarchy just to drive shader generation — a costly and awkward round-trip.

  The issue floated the idea of a "Visitor pattern." We read it and decided to implement something more practical: a pull-based query interface that any backend can implement.

  ---
  Phase 1 — Core abstractions

  Designed and built the three foundational pieces:

  - DataHandle — opaque uint64_t token (0 = InvalidHandle). Lets any backend represent its objects however it wants (raw pointer, index, hash, packed pair) without exposing them
   to the generation code.
  - IShaderSource — pure virtual, read-only interface (~43 methods). Covers node topology, NodeDef/NodeGraph queries, port metadata (values, units, color spaces, geomprops), and
   an optional MX compatibility bridge for the two methods that still need a live MX type.
  - MxElementAdapter — the reference implementation. Wraps a (ConstDocumentPtr, ConstElementPtr) pair and answers all IShaderSource queries by delegating to the existing MX API.

  ---
  Phase 2 — ShaderGraph2 and GenContextCreate

  - ShaderGraph2 — subclass of the existing ShaderGraph. Exposes its protected methods and adds IShaderSource-driven variants for node initialization, input transforms, geomnode
   defaults, and socket construction, so those paths don't need a ConstNodePtr or ConstDocumentPtr.
  - GenContextCreate — thin entry point: takes a generator + unique_ptr<IShaderSource>, exposes buildGraph(name).

  ---
  Phase 3 — ShaderGraphBuilder (BFS traversal)

  Wrote ShaderGraphBuilder, an explicit BFS that replaces ShaderGraph::addUpstreamDependencies. It drives the entire graph walk by pulling data from IShaderSource — no MaterialX
   Element traversal. Handles both the node-root case (surface/material shaders) and the output-root case (NodeGraph outputs).

  Added the first parity tests: 6 materials, GLSL + MDL emit, all matching the original ShaderGraph::create() output.

  ---
  Phase 4 — Eliminating MX dependencies one by one

  Three sub-phases removed progressively deeper MX coupling:

  - Phase 4 (getMxNode removed): Replaced createNode(ConstNodePtr) with initializeNode2, replaced applyInputTransforms(ConstNodePtr) with applyInputTransforms2, replaced
  populateUnitTransformMap(ValueElementPtr) with an IShaderSource port query. Added NoMxNodeAdapter test stub that FAIL()s if getMxNode() is ever called.
  - Phase 4b (getMxDocument removed from buildOutputRoot): Rewrote buildOutputRoot entirely through IShaderSource. Added getNodeGraphInputCount, getNodeGraphInput,
  getOutputParentNodeGraph to the interface, plus addInputSocketsFromNodeDef3 / NodeGraph3 / Node3 helpers in ShaderGraph2.
  - Phase 4c (getMxDocument removed from ShaderGraph2 constructor): Added getMxNodeDefByHandle(DataHandle) to the bridge, added addDefaultGeomNode2 to ShaderGraph2, and changed
  ShaderGraphBuilder::build() to pass nullptr as the document. CountingDocumentAdapter tests updated to assert callCount == 0. The graph is now fully document-free during
  construction.

  At the end of Phase 4c: 341 assertions in 20 test cases, all passing.

  ---
  Bug fix — interfacename chain resolution

  After running the full test suite, one test failed: standard_surface_marble_solid. The bug was in MxElementAdapter::getPortValueString: when a NodeGraph child input had no
  direct value attribute and was only bound via interfacename, the method was falling through to the NodeDef default instead of walking up to the parent NodeGraph's interface
  input. Fixed by adding the interfacename chain walk. Full suite result: 19,020 assertions in 86 test cases, all passing.

  ---
  generate(ShaderGraphPtr) overloads

  Added generate(ShaderGraphPtr) overloads to all four built-in generators (GlslShaderGenerator, MdlShaderGenerator, OslShaderGenerator, MslShaderGenerator). This lets a caller
  who has already built a ShaderGraph2 via ShaderGraphBuilder pass it directly to emit — skipping the graph construction step entirely.

  ---
  Expanded test coverage

  - Added a full-examples sweep (all 6 parity materials across GLSL + MDL).
  - Added OSL and MSL emit parity tests.
  - Documented the known BFS cycle limitation (NodeGraph with output-to-output connections can form a cycle in the BFS; detected and skipped with a WARN).

  ---
  USD/Hydra backend (separate OpenUSD fork)

  Proved the abstraction works end-to-end with a real non-MX backend:

  - Wrote HdMtlxShaderSource : IShaderSource backed by HdMaterialNetwork2 (OpenUSD's material graph type). Handle encoding uses the low 2 bits as a type tag to multiplex three
  pointer types.
  - Added HdSt_GenMaterialXShader(unique_ptr<IShaderSource>, ...) overload to materialXFilter, gated behind PXR_ENABLE_MATERIALX_GENSHADER2.
  - Fixed a nasty MSVC namespace bug: #include <IShaderSource.h> had to land before PXR_NAMESPACE_OPEN_SCOPE or using string = std::string was never visible inside the PXR
  namespace wrapper.
  - Fixed a missing #define AIRY_FRESNEL_ITERATIONS in Storm's shader gen that upstream emits but Storm's reimplementation had omitted.
  - Runtime verified: usdview loaded a Brass_Antique OpenPBR material on a shader ball, rendered correctly in Hydra::Storm + OpenGL. No shader compilation errors.

  Both repos committed: materialx_fpliu @ 6f948361, OpenUSD dev @ fb0b13ae2.

  ---
  Net result

  A new MaterialXGenShader2 library (7 headers + 3 source files, ~3,000 lines) that lets any system drive MaterialX shader generation by implementing a single query interface,
  with no requirement to construct or own a MaterialX Element hierarchy.