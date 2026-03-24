//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXGenShader2/ShaderGraphBuilder.h>

#include <MaterialXGenShader/ShaderGenerator.h>
#include <MaterialXGenShader/ShaderNode.h>

#include <MaterialXCore/Geom.h>
#include <MaterialXCore/Interface.h>
#include <MaterialXCore/Node.h>
#include <MaterialXCore/Value.h>

#include <memory>
#include <queue>

MATERIALX_NAMESPACE_BEGIN

ShaderGraphBuilder::ShaderGraphBuilder(const IShaderSource& source, GenContext& context)
    : _source(source)
    , _context(context)
{
}

// ─── Public entry point ───────────────────────────────────────────────────────

ShaderGraph2Ptr ShaderGraphBuilder::build(const string& name)
{
    DataHandle root = _source.getRootElement();
    if (!isValidHandle(root))
    {
        throw ExceptionShaderGenError("ShaderGraphBuilder: IShaderSource returned an invalid root handle");
    }

    if (_source.isNode(root))
    {
        // Resolve the NodeDef via the MX compatibility bridge.
        // getMxNodeDef() is a required bridge method: NodeDefs live in the loaded
        // shader library and carry classification data (node category, group, version)
        // that ShaderNode::create() needs.  Removing this dependency would require
        // either exposing that metadata through IShaderSource or having backends
        // pre-classify nodes themselves.
        ConstNodeDefPtr nodeDef = _source.getMxNodeDef(root);
        if (!nodeDef)
        {
            throw ExceptionShaderGenError("ShaderGraphBuilder: could not resolve NodeDef for root node '" +
                                          _source.getElementName(root) + "'. Non-MX backends must override getMxNodeDef().");
        }

        ShaderGraph2Ptr graph = std::make_shared<ShaderGraph2>(nullptr, name, nullptr, _context.getReservedWords());
        graph->setClassification2(0);

        buildNodeRoot(*graph, root, nodeDef);

        if (_context.getOptions().addUpstreamDependencies)
        {
            addUpstreamDependencies(*graph, root);
        }

        graph->finalize2(_context);
        return graph;
    }

    if (_source.isOutput(root))
    {
        ShaderGraph2Ptr graph = std::make_shared<ShaderGraph2>(nullptr, name, nullptr, _context.getReservedWords());
        graph->setClassification2(0);

        buildOutputRoot(*graph, root);

        if (_context.getOptions().addUpstreamDependencies)
        {
            addUpstreamDependencies(*graph, root);
        }

        graph->finalize2(_context);
        return graph;
    }

    throw ExceptionShaderGenError("ShaderGraphBuilder: root element '" + _source.getElementName(root) +
                                  "' is neither a Node nor an Output");
}

// ─── Root setup ───────────────────────────────────────────────────────────────

void ShaderGraphBuilder::buildNodeRoot(ShaderGraph2& graph, DataHandle rootNode,
                                        ConstNodeDefPtr nodeDef)
{
    graph.addInputSockets2(*nodeDef, _context);
    graph.addOutputSockets2(*nodeDef, _context);

    // Use the simple createNode2 (name, nodeDef) overload so that
    // defaultgeomprop nodes and applyInputTransforms are NOT applied inside
    // createNode — replicate the root-node setup from ShaderGraph::create() exactly:
    // manual value wiring first, then a single explicit applyInputTransforms at the end.
    const string rootName = _source.getElementName(rootNode);
    const string rootPath = _source.getElementPath(rootNode);
    ShaderNode* shaderNode = graph.createNode2(rootName, nodeDef, _context);
    if (!shaderNode)
    {
        throw ExceptionShaderGenError("ShaderGraphBuilder: createNode failed for '" +
                                      rootName + "'");
    }

    // Share metadata with the graph.
    graph.setMetadata(shaderNode->getMetadata());

    // Wire root node outputs to graph output sockets.
    for (size_t i = 0; i < shaderNode->numOutputs(); ++i)
    {
        ShaderGraphOutputSocket* outputSocket = graph.getOutputSocket(i);
        if (outputSocket)
        {
            outputSocket->makeConnection(shaderNode->getOutput(i));
            outputSocket->setPath(rootPath);
        }
    }

    // Wire graph input sockets to the root node's inputs, copying values/metadata.
    // This replicates the nodeInput loop in ShaderGraph::create() for the Node case.
    // Uses IShaderSource queries instead of a ConstNodePtr to be MX-independent.
    for (InputPtr nodedefInput : nodeDef->getActiveInputs())
    {
        ShaderGraphInputSocket* inputSocket = graph.getInputSocket(nodedefInput->getName());
        ShaderInput* input = shaderNode->getInput(nodedefInput->getName());
        if (!inputSocket || !input)
        {
            continue;
        }

        DataHandle inp = _source.getNodeInputByName(rootNode, nodedefInput->getName());
        if (isValidHandle(inp))
        {
            const string valueStr = _source.getPortValueString(inp);
            if (!valueStr.empty())
            {
                std::pair<TypeDesc, ValuePtr> enumResult;
                const TypeDesc type = _context.getTypeDesc(nodedefInput->getType());
                const string& enumNames = nodedefInput->getAttribute(ValueElement::ENUM_ATTRIBUTE);
                if (_context.getShaderGenerator().getSyntax().remapEnumeration(
                        valueStr, type, enumNames, enumResult))
                {
                    inputSocket->setValue(enumResult.second);
                }
                else
                {
                    ValuePtr value = Value::createValueFromStrings(valueStr, nodedefInput->getType());
                    inputSocket->setValue(value);
                }
            }

            input->setBindInput();

            const string path = _source.getPortPath(inp);
            if (!path.empty())
            {
                inputSocket->setPath(path);
                input->setPath(path);
            }
            const string unit = _source.getPortUnit(inp);
            if (!unit.empty())
            {
                inputSocket->setUnit(unit);
                input->setUnit(unit);
            }
            const string colorSpace = _source.getPortColorSpace(inp);
            if (!colorSpace.empty())
            {
                inputSocket->setColorSpace(colorSpace);
                input->setColorSpace(colorSpace);
            }
        }

        inputSocket->makeConnection(input);
        inputSocket->setMetadata(input->getMetadata());
    }

    // Apply color and unit transforms to the root node's inputs via IShaderSource.
    graph.applyInputTransforms2(rootNode, shaderNode, _source, _context);
}

void ShaderGraphBuilder::buildOutputRoot(ShaderGraph2& graph, DataHandle rootOutput)
{
    const string outputName = _source.getElementName(rootOutput);
    const string outputType = _source.getPortType(rootOutput);

    // Determine the interface for input socket creation using IShaderSource queries.
    // The parent of a root Output is either a NodeGraph or the root Document.
    DataHandle parentGraph = _source.getOutputParentNodeGraph(rootOutput);
    if (isValidHandle(parentGraph))
    {
        // Output lives inside a NodeGraph.
        // Prefer the NodeDef interface; fall back to the NodeGraph's own inputs
        // when no NodeDef is associated (free-standing NodeGraph).
        DataHandle ndH = _source.getNodeGraphNodeDef(parentGraph);
        if (isValidHandle(ndH))
        {
            graph.addInputSocketsFromNodeDef3(ndH, _source, _context);
        }
        else
        {
            graph.addInputSocketsFromNodeGraph3(parentGraph, _source, _context);
        }
    }
    else
    {
        // Output lives directly in the Document.
        // The connected node's inputs become the graph interface.
        DataHandle connNode = _source.getOutputConnectedNode(rootOutput);
        if (!isValidHandle(connNode))
        {
            throw ExceptionShaderGenError(
                "ShaderGraphBuilder: document-level output '" + outputName +
                "' is not connected to any node");
        }
        graph.addInputSocketsFromNode3(connNode, _source, _context);
    }

    // Create the output socket.
    const TypeDesc outTypeDesc = _context.getTypeDesc(outputType);
    ShaderGraphOutputSocket* outputSocket = graph.addOutputSocket(outputName, outTypeDesc);
    outputSocket->setPath(_source.getPortPath(rootOutput));

    const string unit = _source.getPortUnit(rootOutput);
    if (!unit.empty()) outputSocket->setUnit(unit);
    const string cs = _source.getPortColorSpace(rootOutput);
    if (!cs.empty()) outputSocket->setColorSpace(cs);
}

// ─── BFS traversal (replaces ShaderGraph::addUpstreamDependencies) ───────────

void ShaderGraphBuilder::addUpstreamDependencies(ShaderGraph2& graph, DataHandle rootElem)
{
    // Worklist entry: (downstreamElem, upstreamNode, connectingInput).
    struct WorkItem
    {
        DataHandle downstream;     // node or output socket downstream of the edge
        DataHandle upstream;       // upstream node handle
        DataHandle connectingInput; // input on the downstream node (InvalidHandle if downstream is Output)
    };

    // KNOWN LIMITATION: assumes the graph is a DAG. A cycle between two or more
    // nodes (A→B→A) will loop forever because only Output elements are tracked
    // in processedOutputPaths; there is no visited set for node paths.
    // MaterialX documents are required to be acyclic, so this is not a concern
    // for well-formed inputs. A future revision should add a visitedNodePaths
    // set and skip seedFromNode() for already-visited nodes to handle malformed
    // graphs gracefully.
    std::queue<WorkItem> worklist;
    std::set<string> processedOutputPaths; // avoid re-processing graph Output elements

    // ── Lambda: seed worklist from a node's connected inputs ─────────────────
    auto seedFromNode = [&](DataHandle nodeHandle)
    {
        size_t n = _source.getNodeInputCount(nodeHandle);
        for (size_t i = 0; i < n; ++i)
        {
            DataHandle inp   = _source.getNodeInput(nodeHandle, i);
            DataHandle conn  = _source.getInputConnectedNode(inp);
            if (isValidHandle(conn))
            {
                worklist.push({ nodeHandle, conn, inp });
            }
        }
    };

    // Seed the worklist from the root.
    if (_source.isOutput(rootElem))
    {
        DataHandle conn = _source.getOutputConnectedNode(rootElem);
        if (isValidHandle(conn))
        {
            worklist.push({ rootElem, conn, InvalidHandle });
        }
    }
    else if (_source.isNode(rootElem))
    {
        seedFromNode(rootElem);
    }

    // ── BFS ──────────────────────────────────────────────────────────────────
    while (!worklist.empty())
    {
        WorkItem item = worklist.front();
        worklist.pop();

        // If the downstream is an Output we have already processed, skip.
        if (_source.isOutput(item.downstream))
        {
            string path = _source.getElementPath(item.downstream);
            if (!processedOutputPaths.insert(path).second)
            {
                continue; // already processed
            }
            // Jump over the Output to the node it's connected to.
            DataHandle actualNode = _source.getOutputConnectedNode(item.downstream);
            if (!isValidHandle(actualNode))
            {
                continue;
            }
            item.upstream = actualNode;
        }

        // If the upstream itself is an Output element, jump through it.
        if (_source.isOutput(item.upstream))
        {
            string path = _source.getElementPath(item.upstream);
            processedOutputPaths.insert(path);
            DataHandle actualNode = _source.getOutputConnectedNode(item.upstream);
            if (!isValidHandle(actualNode))
            {
                continue;
            }
            item.upstream = actualNode;
        }

        createConnectedNodes(graph, item.downstream, item.upstream, item.connectingInput);

        // Continue BFS into the upstream node's inputs.
        seedFromNode(item.upstream);
    }
}

// ─── Single-edge node creation and connection ─────────────────────────────────

void ShaderGraphBuilder::createConnectedNodes(ShaderGraph2& graph,
                                               DataHandle downstreamElem,
                                               DataHandle upstreamNode,
                                               DataHandle connectingInput)
{
    // ── Create upstream ShaderNode if it doesn't exist ────────────────────────
    // Nodes are keyed by their short element name in _nodeMap (matching
    // ShaderGraph::createNode behavior) so that ShaderGraph::optimize() can
    // correctly erase bypassed nodes via _nodeMap.erase(node->getName()).
    const string nodeName = _source.getElementName(upstreamNode);
    ShaderNode* newNode = graph.getNode(nodeName);
    if (!newNode)
    {
        // Resolve the NodeDef via the MX compatibility bridge.
        // getMxNodeDef() is still required: NodeDefs live in the library and any
        // backend that can drive generation will have them loaded.
        ConstNodeDefPtr nodeDef = _source.getMxNodeDef(upstreamNode);
        if (!nodeDef)
        {
            throw ExceptionShaderGenError(
                "ShaderGraphBuilder: could not resolve NodeDef for upstream node '" +
                nodeName +
                "'. Non-MX backends must override getMxNodeDef().");
        }

        newNode = graph.createNode2(nodeName, nodeDef, _context);
        if (!newNode)
        {
            throw ExceptionShaderGenError(
                "ShaderGraphBuilder: createNode failed for '" + nodeName + "'");
        }

        // Initialize values, paths, interface connections, defaultgeomprops,
        // and transform nodes — all driven through IShaderSource queries.
        graph.initializeNode2(upstreamNode, newNode, nodeDef, _source, _context);
    }

    // ── Identify which output of the upstream node to connect from ────────────
    ShaderOutput* upstreamOutput = nullptr;
    if (isValidHandle(connectingInput))
    {
        const string outputName = _source.getInputConnectedOutputName(connectingInput);
        upstreamOutput = outputName.empty() ? newNode->getOutput()
                                            : newNode->getOutput(outputName);
    }
    if (!upstreamOutput)
    {
        upstreamOutput = newNode->getOutput(); // default first output
    }
    if (!upstreamOutput)
    {
        throw ExceptionShaderGenError(
            "ShaderGraphBuilder: no output found on upstream node '" +
            _source.getElementName(upstreamNode) + "'");
    }

    // ── Wire connection to downstream node input or graph output socket ────────
    if (_source.isNode(downstreamElem))
    {
        const string downstreamName = _source.getElementName(downstreamElem);
        ShaderNode* downstream = graph.getNode(downstreamName);
        if (!downstream)
        {
            return; // downstream not yet created; will be connected when processed
        }
        if (downstream == newNode)
        {
            throw ExceptionShaderGenError(
                "ShaderGraphBuilder: node '" + downstream->getName() +
                "' has itself as upstream — cycle detected");
        }
        if (isValidHandle(connectingInput))
        {
            const string inputName = _source.getPortName(connectingInput);
            ShaderInput* input = downstream->getInput(inputName);
            if (input)
            {
                input->makeConnection(upstreamOutput);
            }
        }
    }
    else
    {
        // Downstream is an Output socket on the graph.
        const string socketName = _source.getElementName(downstreamElem);
        ShaderGraphOutputSocket* outputSocket = graph.getOutputSocket(socketName);
        if (outputSocket)
        {
            outputSocket->makeConnection(upstreamOutput);
        }
    }
}

MATERIALX_NAMESPACE_END
