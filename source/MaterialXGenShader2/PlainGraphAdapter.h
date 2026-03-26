//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MATERIALX_GENSHADER2_PLAINGRAPHADAPTER_H
#define MATERIALX_GENSHADER2_PLAINGRAPHADAPTER_H

/// @file
/// IShaderSource implementation backed by plain data structures instead of
/// MaterialX Element pointers.
///
/// PlainGraphAdapter demonstrates that ShaderGen2 can drive shader generation
/// from a flat graph description (maps of nodes and ports) without constructing
/// a MaterialX Element hierarchy for the material itself.  It requires only:
///   - A library Document (for NodeDef / GeomPropDef lookups)
///   - A flat map of node data (name, path, nodedef name, inputs)
///   - A flat map of port data (name, type, value, connections)
///
/// This is the pattern that non-MX backends (e.g. a Hydra HdMaterialNetwork
/// adapter) would follow: node topology and values come from an external graph
/// representation, while NodeDef metadata comes from the loaded MaterialX
/// standard libraries.
///
/// Handle contract:
///   - Node and instance port handles: sequential uint64_t IDs (1, 2, 3, ...)
///   - NodeDef, GeomPropDef, UnitTypeDef, and their child handles: raw
///     library Element* cast to uint64_t (same scheme as MxElementAdapter)
///
/// The extractFromDocument() factory walks a MaterialX Document and copies all
/// node/input data into the plain structures, simulating what an external graph
/// source would provide.  It is intended for parity testing.

#include <MaterialXGenShader2/IShaderSource.h>

#include <MaterialXCore/Document.h>

#include <unordered_map>

MATERIALX_NAMESPACE_BEGIN

/// Plain data describing a single port (input) on a node instance.
struct MX_GENSHADER2_API PlainPort
{
    string name;
    string type;
    string path;
    string valueString;
    bool   hasValue = false;
    DataHandle connectedNode = InvalidHandle;
    string connectedOutputName;
    string interfaceName;
    DataHandle interfaceInput = InvalidHandle;
    string unit;
    string unitType;
    string activeUnit;
    string colorSpace;
    string activeColorSpace;
    bool   isUniform = false;
};

/// Element kind for graph elements stored in the plain map.
enum class PlainElementKind
{
    Node,
    Output,
    NodeGraph
};

/// Plain data describing a node, output, or node graph in the material graph.
struct MX_GENSHADER2_API PlainNode
{
    string name;
    string path;
    PlainElementKind kind = PlainElementKind::Node;
    string nodeDefName;
    vector<DataHandle> inputs;
    DataHandle parentGraph = InvalidHandle;

    // Output elements
    DataHandle outputConnectedNode = InvalidHandle;
    DataHandle outputParentNodeGraph = InvalidHandle;

    // NodeGraph elements
    string nodeGraphNodeDefName;
    vector<DataHandle> nodeGraphInputs;
};

/// @class PlainGraphAdapter
/// IShaderSource backed by plain node/port maps and a library Document.
class MX_GENSHADER2_API PlainGraphAdapter : public IShaderSource
{
  public:
    /// Construct from pre-built data.
    PlainGraphAdapter(ConstDocumentPtr library,
                      DataHandle rootHandle,
                      std::unordered_map<DataHandle, PlainNode> nodes,
                      std::unordered_map<DataHandle, PlainPort> ports,
                      string activeColorSpace);

    /// Walk a MaterialX Document and extract graph data into plain structs.
    /// @param library      The standard library (used for NodeDef lookups at runtime).
    /// @param materialDoc  The material document containing the graph.
    /// @param rootElement  The root Node or Output to build the graph from.
    static PlainGraphAdapter extractFromDocument(
        ConstDocumentPtr library,
        DocumentPtr materialDoc,
        ElementPtr rootElement);

    // --- Root -----------------------------------------------------------------
    DataHandle getRootElement() const override;

    // --- Element classification -----------------------------------------------
    bool isNode(DataHandle elem) const override;
    bool isOutput(DataHandle elem) const override;
    bool isNodeGraph(DataHandle elem) const override;

    // --- Element identity -----------------------------------------------------
    string getElementName(DataHandle elem) const override;
    string getElementPath(DataHandle elem) const override;

    // --- Node topology --------------------------------------------------------
    size_t   getNodeInputCount(DataHandle node) const override;
    DataHandle getNodeInput(DataHandle node, size_t index) const override;
    DataHandle getNodeInputByName(DataHandle node, const string& name) const override;
    DataHandle getInputConnectedNode(DataHandle input) const override;
    string   getInputConnectedOutputName(DataHandle input) const override;
    DataHandle getOutputConnectedNode(DataHandle output) const override;
    DataHandle getNodeParentGraph(DataHandle node) const override;

    // --- Node definition lookup -----------------------------------------------
    string   getNodeDefName(DataHandle node) const override;
    DataHandle getNodeDef(DataHandle node) const override;
    DataHandle getNodeDefByName(const string& nodeDefName) const override;

    // --- NodeDef interface (delegates to library Element*) --------------------
    string   getNodeDefType(DataHandle nodeDef) const override;
    size_t   getNodeDefInputCount(DataHandle nodeDef) const override;
    DataHandle getNodeDefInput(DataHandle nodeDef, size_t index) const override;
    DataHandle getNodeDefInputByName(DataHandle nodeDef, const string& name) const override;
    size_t   getNodeDefOutputCount(DataHandle nodeDef) const override;
    DataHandle getNodeDefOutput(DataHandle nodeDef, size_t index) const override;
    DataHandle getNodeDefOutputByName(DataHandle nodeDef, const string& name) const override;
    size_t   getNodeDefValueElementCount(DataHandle nodeDef) const override;
    DataHandle getNodeDefValueElement(DataHandle nodeDef, size_t index) const override;
    bool     valueElementIsOutput(DataHandle valueElem) const override;
    string   getNodeDefAttribute(DataHandle nodeDef, const string& attrName) const override;
    void     getNodeDefAttributeNames(DataHandle nodeDef, StringVec& names) const override;

    // --- NodeGraph interface --------------------------------------------------
    DataHandle getNodeGraphNodeDef(DataHandle nodeGraph) const override;
    string   getNodeGraphName(DataHandle nodeGraph) const override;
    size_t   getNodeGraphInputCount(DataHandle nodeGraph) const override;
    DataHandle getNodeGraphInput(DataHandle nodeGraph, size_t index) const override;
    DataHandle getOutputParentNodeGraph(DataHandle output) const override;

    // --- Port queries (dispatches plain handles vs library Element*) ----------
    string   getPortName(DataHandle port) const override;
    string   getPortType(DataHandle port) const override;
    string   getPortPath(DataHandle port) const override;
    string   getPortValueString(DataHandle port) const override;
    bool     portHasValue(DataHandle port) const override;
    string   getPortAttribute(DataHandle port, const string& attrName) const override;
    void     getPortAttributeNames(DataHandle port, StringVec& names) const override;

    // --- Interface binding ----------------------------------------------------
    bool     portHasInterfaceName(DataHandle port) const override;
    string   getPortInterfaceName(DataHandle port) const override;
    DataHandle getPortInterfaceInput(DataHandle port) const override;

    // --- Geometric default property -------------------------------------------
    bool     portHasDefaultGeomProp(DataHandle port) const override;
    DataHandle getPortDefaultGeomProp(DataHandle port) const override;

    // --- Unit metadata --------------------------------------------------------
    string   getPortUnit(DataHandle port) const override;
    string   getPortUnitType(DataHandle port) const override;
    string   getPortActiveUnit(DataHandle port) const override;

    // --- Color space metadata -------------------------------------------------
    string   getPortColorSpace(DataHandle port) const override;
    string   getPortActiveColorSpace(DataHandle port) const override;

    // --- Uniformity -----------------------------------------------------------
    bool portIsUniform(DataHandle port) const override;

    // --- GeomPropDef queries (delegates to library Element*) -----------------
    string getGeomPropDefName(DataHandle geomPropDef) const override;
    string getGeomPropDefProp(DataHandle geomPropDef) const override;
    string getGeomPropDefPath(DataHandle geomPropDef) const override;
    string getGeomPropDefSpace(DataHandle geomPropDef) const override;
    string getGeomPropDefIndex(DataHandle geomPropDef) const override;

    // --- Document-level queries -----------------------------------------------
    string   getActiveColorSpace() const override;
    DataHandle getUnitTypeDefByName(const string& unitTypeName) const override;

    // --- MX compatibility bridge ----------------------------------------------
    ConstDocumentPtr getMxDocument() const override { return _library; }
    ConstNodeDefPtr  getMxNodeDef(DataHandle node) const override;
    ConstNodeDefPtr  getMxNodeDefByHandle(DataHandle nodeDefHandle) const override;
    ConstNodePtr     getMxNode(DataHandle /*node*/) const override { return nullptr; }

  private:
    static const Element* toElement(DataHandle h)
    {
        return reinterpret_cast<const Element*>(static_cast<uintptr_t>(h));
    }
    static DataHandle toHandle(const Element* e)
    {
        return e ? static_cast<DataHandle>(reinterpret_cast<uintptr_t>(e)) : InvalidHandle;
    }

    ConstDocumentPtr _library;
    DataHandle _rootHandle;
    std::unordered_map<DataHandle, PlainNode> _nodes;
    std::unordered_map<DataHandle, PlainPort> _ports;
    string _activeColorSpace;
};

MATERIALX_NAMESPACE_END

#endif // MATERIALX_GENSHADER2_PLAINGRAPHADAPTER_H
