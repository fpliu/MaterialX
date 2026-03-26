//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXGenShader2/PlainGraphAdapter.h>

#include <MaterialXCore/Definition.h>
#include <MaterialXCore/Geom.h>
#include <MaterialXCore/Interface.h>
#include <MaterialXCore/Node.h>
#include <MaterialXCore/Unit.h>

#include <functional>
#include <queue>

MATERIALX_NAMESPACE_BEGIN

// --- Construction -------------------------------------------------------------

PlainGraphAdapter::PlainGraphAdapter(
    ConstDocumentPtr library,
    DataHandle rootHandle,
    std::unordered_map<DataHandle, PlainNode> nodes,
    std::unordered_map<DataHandle, PlainPort> ports,
    string activeColorSpace)
    : _library(std::move(library))
    , _rootHandle(rootHandle)
    , _nodes(std::move(nodes))
    , _ports(std::move(ports))
    , _activeColorSpace(std::move(activeColorSpace))
{
}

// --- extractFromDocument factory ----------------------------------------------

PlainGraphAdapter PlainGraphAdapter::extractFromDocument(
    ConstDocumentPtr library,
    DocumentPtr materialDoc,
    ElementPtr rootElement)
{
    DataHandle nextHandle = 1;
    std::unordered_map<string, DataHandle> pathToNodeHandle;
    std::unordered_map<DataHandle, PlainNode> nodes;
    std::unordered_map<DataHandle, PlainPort> ports;

    // Temporary storage: port handles that need connection resolution.
    // Stores (portHandle, connectedNodePath) pairs.
    vector<std::pair<DataHandle, string>> deferredConnections;
    // Interface input resolution: (portHandle, interfaceInputPath) pairs.
    vector<std::pair<DataHandle, string>> deferredInterfaceInputs;

    // Helper: extract port data from a MaterialX Input.
    auto extractPort = [&](InputPtr inp, DataHandle /*ownerNode*/) -> DataHandle
    {
        DataHandle portH = nextHandle++;
        PlainPort pd;
        pd.name = inp->getName();
        pd.type = inp->getType();
        pd.path = inp->getNamePath();
        pd.hasValue = inp->hasValue();

        // Resolve value string: follow interfacename binding if present.
        if (inp->hasInterfaceName())
        {
            InputPtr ifaceInput = inp->getInterfaceInput();
            if (ifaceInput)
            {
                ValuePtr val = ifaceInput->getResolvedValue();
                pd.valueString = val ? val->getValueString() : EMPTY_STRING;
            }
        }
        else
        {
            ValuePtr val = inp->getResolvedValue();
            pd.valueString = val ? val->getValueString() : EMPTY_STRING;
        }

        // Connection (node path stored temporarily, resolved to handle later).
        NodePtr connNode = inp->getConnectedNode();
        if (connNode)
        {
            deferredConnections.push_back({portH, connNode->getNamePath()});
        }

        pd.connectedOutputName = inp->getOutputString();
        pd.interfaceName = inp->getInterfaceName();

        // Interface input resolution (deferred).
        if (inp->hasInterfaceName())
        {
            InputPtr ifaceInput = inp->getInterfaceInput();
            if (ifaceInput)
            {
                deferredInterfaceInputs.push_back({portH, ifaceInput->getNamePath()});
            }
        }

        pd.unit = inp->getUnit();
        pd.unitType = inp->getUnitType();
        pd.activeUnit = inp->getActiveUnit();
        pd.colorSpace = inp->getColorSpace();
        pd.activeColorSpace = inp->getActiveColorSpace();
        pd.isUniform = inp->getIsUniform();

        ports[portH] = std::move(pd);
        return portH;
    };

    // Forward declaration: extractNodeGraph may be called from extractNode.
    std::function<DataHandle(NodeGraphPtr)> extractNodeGraph;

    // Helper: create a PlainNode from a MaterialX Node.
    auto extractNode = [&](NodePtr node) -> DataHandle
    {
        string nodePath = node->getNamePath();
        auto existing = pathToNodeHandle.find(nodePath);
        if (existing != pathToNodeHandle.end())
        {
            return existing->second;
        }

        DataHandle nodeH = nextHandle++;
        PlainNode nd;
        nd.name = node->getName();
        nd.path = nodePath;
        nd.kind = PlainElementKind::Node;

        NodeDefPtr nodeDef = node->getNodeDef();
        nd.nodeDefName = nodeDef ? nodeDef->getName() : EMPTY_STRING;

        // If node lives inside a NodeGraph, extract the parent NodeGraph first
        // so that interface input handles are available for resolution.
        ElementPtr parent = node->getParent();
        if (parent && parent->isA<NodeGraph>())
        {
            DataHandle ngH = extractNodeGraph(parent->asA<NodeGraph>());
            nd.parentGraph = ngH;
        }

        pathToNodeHandle[nodePath] = nodeH;

        // Extract inputs.
        for (InputPtr inp : node->getActiveInputs())
        {
            DataHandle portH = extractPort(inp, nodeH);
            nd.inputs.push_back(portH);
        }

        nodes[nodeH] = std::move(nd);
        return nodeH;
    };

    // Helper: create a PlainNode for an Output element.
    auto extractOutput = [&](OutputPtr output) -> DataHandle
    {
        string outputPath = output->getNamePath();
        auto existing = pathToNodeHandle.find(outputPath);
        if (existing != pathToNodeHandle.end())
        {
            return existing->second;
        }

        DataHandle outputH = nextHandle++;
        PlainNode nd;
        nd.name = output->getName();
        nd.path = outputPath;
        nd.kind = PlainElementKind::Output;

        NodePtr connNode = output->getConnectedNode();
        if (connNode)
        {
            deferredConnections.push_back({outputH, connNode->getNamePath()});
        }

        ElementPtr parent = output->getParent();
        if (parent && parent->isA<NodeGraph>())
        {
            string parentPath = parent->getNamePath();
            auto pit = pathToNodeHandle.find(parentPath);
            if (pit != pathToNodeHandle.end())
            {
                nd.outputParentNodeGraph = pit->second;
            }
        }

        pathToNodeHandle[outputPath] = outputH;
        nodes[outputH] = std::move(nd);
        return outputH;
    };

    // Helper: create a PlainNode for a NodeGraph element.
    extractNodeGraph = [&](NodeGraphPtr nodeGraph) -> DataHandle
    {
        string ngPath = nodeGraph->getNamePath();
        auto existing = pathToNodeHandle.find(ngPath);
        if (existing != pathToNodeHandle.end())
        {
            return existing->second;
        }

        DataHandle ngH = nextHandle++;
        PlainNode nd;
        nd.name = nodeGraph->getName();
        nd.path = ngPath;
        nd.kind = PlainElementKind::NodeGraph;

        NodeDefPtr ngNodeDef = nodeGraph->getNodeDef();
        nd.nodeGraphNodeDefName = ngNodeDef ? ngNodeDef->getName() : EMPTY_STRING;

        for (InputPtr inp : nodeGraph->getActiveInputs())
        {
            DataHandle portH = extractPort(inp, ngH);
            nd.nodeGraphInputs.push_back(portH);
        }

        pathToNodeHandle[ngPath] = ngH;
        nodes[ngH] = std::move(nd);
        return ngH;
    };

    // --- BFS extraction from root element ---

    DataHandle rootH = InvalidHandle;

    if (NodePtr rootNode = rootElement->asA<Node>())
    {
        rootH = extractNode(rootNode);

        // BFS to discover all upstream nodes.
        std::queue<NodePtr> worklist;
        for (InputPtr inp : rootNode->getActiveInputs())
        {
            NodePtr conn = inp->getConnectedNode();
            if (conn && pathToNodeHandle.find(conn->getNamePath()) == pathToNodeHandle.end())
            {
                worklist.push(conn);
            }
        }

        while (!worklist.empty())
        {
            NodePtr node = worklist.front();
            worklist.pop();
            if (pathToNodeHandle.count(node->getNamePath()))
            {
                continue;
            }

            extractNode(node);

            for (InputPtr inp : node->getActiveInputs())
            {
                NodePtr conn = inp->getConnectedNode();
                if (conn && pathToNodeHandle.find(conn->getNamePath()) == pathToNodeHandle.end())
                {
                    worklist.push(conn);
                }
            }
        }
    }
    else if (OutputPtr rootOutput = rootElement->asA<Output>())
    {
        // For output-rooted graphs: extract the parent NodeGraph if present,
        // then the output, then BFS all upstream nodes.
        ElementPtr parent = rootOutput->getParent();
        if (parent && parent->isA<NodeGraph>())
        {
            extractNodeGraph(parent->asA<NodeGraph>());
        }

        rootH = extractOutput(rootOutput);

        // BFS upstream from the output's connected node.
        NodePtr connNode = rootOutput->getConnectedNode();
        if (connNode)
        {
            std::queue<NodePtr> worklist;
            if (pathToNodeHandle.find(connNode->getNamePath()) == pathToNodeHandle.end())
            {
                worklist.push(connNode);
            }
            while (!worklist.empty())
            {
                NodePtr node = worklist.front();
                worklist.pop();
                if (pathToNodeHandle.count(node->getNamePath()))
                {
                    continue;
                }

                extractNode(node);

                for (InputPtr inp : node->getActiveInputs())
                {
                    NodePtr conn = inp->getConnectedNode();
                    if (conn && pathToNodeHandle.find(conn->getNamePath()) == pathToNodeHandle.end())
                    {
                        worklist.push(conn);
                    }
                }
            }
        }
    }

    // --- Resolve deferred connections (path → handle) ---

    for (auto& [handle, connPath] : deferredConnections)
    {
        auto it = pathToNodeHandle.find(connPath);
        if (it == pathToNodeHandle.end())
        {
            continue;
        }
        DataHandle connH = it->second;

        // Determine if the handle is a port or a node (for Output elements).
        auto portIt = ports.find(handle);
        if (portIt != ports.end())
        {
            portIt->second.connectedNode = connH;
        }
        else
        {
            auto nodeIt = nodes.find(handle);
            if (nodeIt != nodes.end() && nodeIt->second.kind == PlainElementKind::Output)
            {
                nodeIt->second.outputConnectedNode = connH;
            }
        }
    }

    // --- Resolve deferred interface inputs (path → port handle) ---

    // Build a path → port handle map for interface input resolution.
    std::unordered_map<string, DataHandle> pathToPortHandle;
    for (auto& [portH, pd] : ports)
    {
        if (!pd.path.empty())
        {
            pathToPortHandle[pd.path] = portH;
        }
    }

    for (auto& [portH, ifacePath] : deferredInterfaceInputs)
    {
        auto it = pathToPortHandle.find(ifacePath);
        if (it != pathToPortHandle.end())
        {
            ports[portH].interfaceInput = it->second;
        }
    }

    // --- Resolve parent graph handles for nodes ---

    for (auto& [nodeH, nd] : nodes)
    {
        if (nd.kind == PlainElementKind::Node && nd.parentGraph == InvalidHandle)
        {
            // Try to find the parent graph by checking if any NodeGraph path is a prefix.
            // (Already handled during extraction for nodes created after their parent.)
        }
    }

    string activeColorSpace = materialDoc->getActiveColorSpace();
    return PlainGraphAdapter(library, rootH, std::move(nodes), std::move(ports), std::move(activeColorSpace));
}

// --- Root ---------------------------------------------------------------------

DataHandle PlainGraphAdapter::getRootElement() const
{
    return _rootHandle;
}

// --- Element classification ---------------------------------------------------

bool PlainGraphAdapter::isNode(DataHandle elem) const
{
    auto it = _nodes.find(elem);
    return it != _nodes.end() && it->second.kind == PlainElementKind::Node;
}

bool PlainGraphAdapter::isOutput(DataHandle elem) const
{
    auto it = _nodes.find(elem);
    return it != _nodes.end() && it->second.kind == PlainElementKind::Output;
}

bool PlainGraphAdapter::isNodeGraph(DataHandle elem) const
{
    auto it = _nodes.find(elem);
    return it != _nodes.end() && it->second.kind == PlainElementKind::NodeGraph;
}

// --- Element identity ---------------------------------------------------------

string PlainGraphAdapter::getElementName(DataHandle elem) const
{
    auto it = _nodes.find(elem);
    return it != _nodes.end() ? it->second.name : EMPTY_STRING;
}

string PlainGraphAdapter::getElementPath(DataHandle elem) const
{
    auto it = _nodes.find(elem);
    return it != _nodes.end() ? it->second.path : EMPTY_STRING;
}

// --- Node topology ------------------------------------------------------------

size_t PlainGraphAdapter::getNodeInputCount(DataHandle node) const
{
    auto it = _nodes.find(node);
    return it != _nodes.end() ? it->second.inputs.size() : 0;
}

DataHandle PlainGraphAdapter::getNodeInput(DataHandle node, size_t index) const
{
    auto it = _nodes.find(node);
    if (it == _nodes.end() || index >= it->second.inputs.size())
    {
        return InvalidHandle;
    }
    return it->second.inputs[index];
}

DataHandle PlainGraphAdapter::getNodeInputByName(DataHandle node, const string& name) const
{
    auto it = _nodes.find(node);
    if (it == _nodes.end())
    {
        return InvalidHandle;
    }
    for (DataHandle portH : it->second.inputs)
    {
        auto pit = _ports.find(portH);
        if (pit != _ports.end() && pit->second.name == name)
        {
            return portH;
        }
    }
    return InvalidHandle;
}

DataHandle PlainGraphAdapter::getInputConnectedNode(DataHandle input) const
{
    auto it = _ports.find(input);
    return (it != _ports.end()) ? it->second.connectedNode : InvalidHandle;
}

string PlainGraphAdapter::getInputConnectedOutputName(DataHandle input) const
{
    auto it = _ports.find(input);
    return (it != _ports.end()) ? it->second.connectedOutputName : EMPTY_STRING;
}

DataHandle PlainGraphAdapter::getOutputConnectedNode(DataHandle output) const
{
    auto it = _nodes.find(output);
    if (it != _nodes.end() && it->second.kind == PlainElementKind::Output)
    {
        return it->second.outputConnectedNode;
    }
    return InvalidHandle;
}

DataHandle PlainGraphAdapter::getNodeParentGraph(DataHandle node) const
{
    auto it = _nodes.find(node);
    return (it != _nodes.end()) ? it->second.parentGraph : InvalidHandle;
}

// --- Node definition lookup ---------------------------------------------------

string PlainGraphAdapter::getNodeDefName(DataHandle node) const
{
    auto it = _nodes.find(node);
    return (it != _nodes.end()) ? it->second.nodeDefName : EMPTY_STRING;
}

DataHandle PlainGraphAdapter::getNodeDef(DataHandle node) const
{
    auto it = _nodes.find(node);
    if (it == _nodes.end() || it->second.nodeDefName.empty())
    {
        return InvalidHandle;
    }
    ConstNodeDefPtr nd = _library->getNodeDef(it->second.nodeDefName);
    return toHandle(nd.get());
}

DataHandle PlainGraphAdapter::getNodeDefByName(const string& nodeDefName) const
{
    return toHandle(_library->getNodeDef(nodeDefName).get());
}

// --- NodeDef interface (all delegate to library Element*) ---------------------

string PlainGraphAdapter::getNodeDefType(DataHandle nodeDef) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return EMPTY_STRING;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    return nd ? nd->getType() : EMPTY_STRING;
}

size_t PlainGraphAdapter::getNodeDefInputCount(DataHandle nodeDef) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return 0;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    return nd ? nd->getActiveInputs().size() : 0;
}

DataHandle PlainGraphAdapter::getNodeDefInput(DataHandle nodeDef, size_t index) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return InvalidHandle;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    if (!nd) return InvalidHandle;
    vector<InputPtr> inputs = nd->getActiveInputs();
    if (index >= inputs.size()) return InvalidHandle;
    return toHandle(inputs[index].get());
}

DataHandle PlainGraphAdapter::getNodeDefInputByName(DataHandle nodeDef, const string& name) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return InvalidHandle;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    if (!nd) return InvalidHandle;
    return toHandle(nd->getInput(name).get());
}

size_t PlainGraphAdapter::getNodeDefOutputCount(DataHandle nodeDef) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return 0;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    return nd ? nd->getActiveOutputs().size() : 0;
}

DataHandle PlainGraphAdapter::getNodeDefOutput(DataHandle nodeDef, size_t index) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return InvalidHandle;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    if (!nd) return InvalidHandle;
    vector<OutputPtr> outputs = nd->getActiveOutputs();
    if (index >= outputs.size()) return InvalidHandle;
    return toHandle(outputs[index].get());
}

DataHandle PlainGraphAdapter::getNodeDefOutputByName(DataHandle nodeDef, const string& name) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return InvalidHandle;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    if (!nd) return InvalidHandle;
    return toHandle(nd->getOutput(name).get());
}

size_t PlainGraphAdapter::getNodeDefValueElementCount(DataHandle nodeDef) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return 0;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    return nd ? nd->getActiveValueElements().size() : 0;
}

DataHandle PlainGraphAdapter::getNodeDefValueElement(DataHandle nodeDef, size_t index) const
{
    const Element* e = toElement(nodeDef);
    if (!e) return InvalidHandle;
    ConstNodeDefPtr nd = e->asA<NodeDef>();
    if (!nd) return InvalidHandle;
    vector<ValueElementPtr> elems = nd->getActiveValueElements();
    if (index >= elems.size()) return InvalidHandle;
    return toHandle(elems[index].get());
}

bool PlainGraphAdapter::valueElementIsOutput(DataHandle valueElem) const
{
    const Element* e = toElement(valueElem);
    return e && e->isA<Output>();
}

string PlainGraphAdapter::getNodeDefAttribute(DataHandle nodeDef, const string& attrName) const
{
    const Element* e = toElement(nodeDef);
    return e ? e->getAttribute(attrName) : EMPTY_STRING;
}

void PlainGraphAdapter::getNodeDefAttributeNames(DataHandle nodeDef, StringVec& names) const
{
    const Element* e = toElement(nodeDef);
    if (e)
    {
        names = e->getAttributeNames();
    }
}

// --- NodeGraph interface ------------------------------------------------------

DataHandle PlainGraphAdapter::getNodeGraphNodeDef(DataHandle nodeGraph) const
{
    auto it = _nodes.find(nodeGraph);
    if (it == _nodes.end() || it->second.kind != PlainElementKind::NodeGraph)
    {
        return InvalidHandle;
    }
    if (it->second.nodeGraphNodeDefName.empty())
    {
        return InvalidHandle;
    }
    return toHandle(_library->getNodeDef(it->second.nodeGraphNodeDefName).get());
}

string PlainGraphAdapter::getNodeGraphName(DataHandle nodeGraph) const
{
    auto it = _nodes.find(nodeGraph);
    return (it != _nodes.end() && it->second.kind == PlainElementKind::NodeGraph)
        ? it->second.name : EMPTY_STRING;
}

size_t PlainGraphAdapter::getNodeGraphInputCount(DataHandle nodeGraph) const
{
    auto it = _nodes.find(nodeGraph);
    return (it != _nodes.end() && it->second.kind == PlainElementKind::NodeGraph)
        ? it->second.nodeGraphInputs.size() : 0;
}

DataHandle PlainGraphAdapter::getNodeGraphInput(DataHandle nodeGraph, size_t index) const
{
    auto it = _nodes.find(nodeGraph);
    if (it == _nodes.end() || it->second.kind != PlainElementKind::NodeGraph ||
        index >= it->second.nodeGraphInputs.size())
    {
        return InvalidHandle;
    }
    return it->second.nodeGraphInputs[index];
}

DataHandle PlainGraphAdapter::getOutputParentNodeGraph(DataHandle output) const
{
    auto it = _nodes.find(output);
    if (it != _nodes.end() && it->second.kind == PlainElementKind::Output)
    {
        return it->second.outputParentNodeGraph;
    }
    return InvalidHandle;
}

// --- Port queries (dispatch: plain handle → stored data, else → library Element*) ---

string PlainGraphAdapter::getPortName(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.name;
    }
    const Element* e = toElement(port);
    return e ? e->getName() : EMPTY_STRING;
}

string PlainGraphAdapter::getPortType(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.type;
    }
    const Element* e = toElement(port);
    if (!e) return EMPTY_STRING;
    shared_ptr<const ValueElement> ve = e->asA<ValueElement>();
    return ve ? ve->getType() : EMPTY_STRING;
}

string PlainGraphAdapter::getPortPath(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.path;
    }
    const Element* e = toElement(port);
    return e ? e->getNamePath() : EMPTY_STRING;
}

string PlainGraphAdapter::getPortValueString(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.valueString;
    }
    // Library element (NodeDef input): delegate to Element API.
    const Element* e = toElement(port);
    if (!e) return EMPTY_STRING;
    shared_ptr<const ValueElement> ve = e->asA<ValueElement>();
    if (!ve) return EMPTY_STRING;
    ValuePtr val = ve->getResolvedValue();
    return val ? val->getValueString() : EMPTY_STRING;
}

bool PlainGraphAdapter::portHasValue(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.hasValue;
    }
    const Element* e = toElement(port);
    if (!e) return false;
    shared_ptr<const ValueElement> ve = e->asA<ValueElement>();
    return ve && ve->hasValue();
}

string PlainGraphAdapter::getPortAttribute(DataHandle port, const string& attrName) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return EMPTY_STRING; // Instance inputs don't carry XML attributes.
    }
    const Element* e = toElement(port);
    return e ? e->getAttribute(attrName) : EMPTY_STRING;
}

void PlainGraphAdapter::getPortAttributeNames(DataHandle port, StringVec& names) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        names.clear();
        return;
    }
    const Element* e = toElement(port);
    if (e)
    {
        names = e->getAttributeNames();
    }
}

// --- Interface binding --------------------------------------------------------

bool PlainGraphAdapter::portHasInterfaceName(DataHandle port) const
{
    auto it = _ports.find(port);
    return it != _ports.end() && !it->second.interfaceName.empty();
}

string PlainGraphAdapter::getPortInterfaceName(DataHandle port) const
{
    auto it = _ports.find(port);
    return (it != _ports.end()) ? it->second.interfaceName : EMPTY_STRING;
}

DataHandle PlainGraphAdapter::getPortInterfaceInput(DataHandle port) const
{
    auto it = _ports.find(port);
    return (it != _ports.end()) ? it->second.interfaceInput : InvalidHandle;
}

// --- Geometric default property -----------------------------------------------

bool PlainGraphAdapter::portHasDefaultGeomProp(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return false; // Instance inputs don't carry defaultgeomprop.
    }
    const Element* e = toElement(port);
    if (!e) return false;
    shared_ptr<const Input> inp = e->asA<Input>();
    return inp && inp->hasDefaultGeomPropString();
}

DataHandle PlainGraphAdapter::getPortDefaultGeomProp(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return InvalidHandle;
    }
    const Element* e = toElement(port);
    if (!e) return InvalidHandle;
    shared_ptr<const Input> inp = e->asA<Input>();
    if (!inp) return InvalidHandle;
    return toHandle(inp->getDefaultGeomProp().get());
}

// --- Unit metadata ------------------------------------------------------------

string PlainGraphAdapter::getPortUnit(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.unit;
    }
    const Element* e = toElement(port);
    if (!e) return EMPTY_STRING;
    shared_ptr<const Input> inp = e->asA<Input>();
    return inp ? inp->getUnit() : EMPTY_STRING;
}

string PlainGraphAdapter::getPortUnitType(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.unitType;
    }
    const Element* e = toElement(port);
    if (!e) return EMPTY_STRING;
    shared_ptr<const Input> inp = e->asA<Input>();
    return inp ? inp->getUnitType() : EMPTY_STRING;
}

string PlainGraphAdapter::getPortActiveUnit(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.activeUnit;
    }
    const Element* e = toElement(port);
    if (!e) return EMPTY_STRING;
    shared_ptr<const Input> inp = e->asA<Input>();
    return inp ? inp->getActiveUnit() : EMPTY_STRING;
}

// --- Color space metadata -----------------------------------------------------

string PlainGraphAdapter::getPortColorSpace(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.colorSpace;
    }
    const Element* e = toElement(port);
    if (!e) return EMPTY_STRING;
    shared_ptr<const Input> inp = e->asA<Input>();
    return inp ? inp->getColorSpace() : EMPTY_STRING;
}

string PlainGraphAdapter::getPortActiveColorSpace(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.activeColorSpace;
    }
    const Element* e = toElement(port);
    if (!e) return EMPTY_STRING;
    shared_ptr<const Input> inp = e->asA<Input>();
    return inp ? inp->getActiveColorSpace() : EMPTY_STRING;
}

// --- Uniformity ---------------------------------------------------------------

bool PlainGraphAdapter::portIsUniform(DataHandle port) const
{
    auto it = _ports.find(port);
    if (it != _ports.end())
    {
        return it->second.isUniform;
    }
    const Element* e = toElement(port);
    if (!e) return false;
    shared_ptr<const Input> inp = e->asA<Input>();
    return inp && inp->getIsUniform();
}

// --- GeomPropDef queries (delegate to library Element*) -----------------------

string PlainGraphAdapter::getGeomPropDefName(DataHandle geomPropDef) const
{
    const Element* e = toElement(geomPropDef);
    return e ? e->getName() : EMPTY_STRING;
}

string PlainGraphAdapter::getGeomPropDefProp(DataHandle geomPropDef) const
{
    const Element* e = toElement(geomPropDef);
    if (!e) return EMPTY_STRING;
    ConstGeomPropDefPtr gp = e->asA<GeomPropDef>();
    return gp ? gp->getGeomProp() : EMPTY_STRING;
}

string PlainGraphAdapter::getGeomPropDefPath(DataHandle geomPropDef) const
{
    const Element* e = toElement(geomPropDef);
    return e ? e->getNamePath() : EMPTY_STRING;
}

string PlainGraphAdapter::getGeomPropDefSpace(DataHandle geomPropDef) const
{
    const Element* e = toElement(geomPropDef);
    if (!e) return EMPTY_STRING;
    ConstGeomPropDefPtr gp = e->asA<GeomPropDef>();
    return gp ? gp->getSpace() : EMPTY_STRING;
}

string PlainGraphAdapter::getGeomPropDefIndex(DataHandle geomPropDef) const
{
    const Element* e = toElement(geomPropDef);
    if (!e) return EMPTY_STRING;
    ConstGeomPropDefPtr gp = e->asA<GeomPropDef>();
    return gp ? gp->getIndex() : EMPTY_STRING;
}

// --- Document-level queries ---------------------------------------------------

string PlainGraphAdapter::getActiveColorSpace() const
{
    return _activeColorSpace;
}

DataHandle PlainGraphAdapter::getUnitTypeDefByName(const string& unitTypeName) const
{
    if (!_library)
    {
        return InvalidHandle;
    }
    return toHandle(_library->getUnitTypeDef(unitTypeName).get());
}

// --- MX compatibility bridge --------------------------------------------------

ConstNodeDefPtr PlainGraphAdapter::getMxNodeDef(DataHandle node) const
{
    auto it = _nodes.find(node);
    if (it == _nodes.end() || it->second.nodeDefName.empty())
    {
        return nullptr;
    }
    return _library->getNodeDef(it->second.nodeDefName);
}

ConstNodeDefPtr PlainGraphAdapter::getMxNodeDefByHandle(DataHandle nodeDefHandle) const
{
    const Element* e = toElement(nodeDefHandle);
    return e ? e->asA<NodeDef>() : nullptr;
}

MATERIALX_NAMESPACE_END
