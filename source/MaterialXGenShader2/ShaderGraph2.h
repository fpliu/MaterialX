//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MATERIALX_GENSHADER2_SHADERGRAPH2_H
#define MATERIALX_GENSHADER2_SHADERGRAPH2_H

/// @file
/// Thin subclass of ShaderGraph that elevates protected methods needed by
/// ShaderGraphBuilder without touching the existing ShaderGraph API.
///
/// Several ShaderGraph methods required for graph construction
/// (finalize, addInputSockets, addOutputSockets) are protected in the base
/// class.  ShaderGraph2 exposes them via public forwarding methods so that
/// ShaderGraphBuilder — living in a different module — can call them.
///
/// ShaderGraph2 IS-A ShaderGraph: it can be stored in a ShaderGraphPtr and
/// used anywhere a ShaderGraph* is expected.

#include <MaterialXGenShader2/DataHandle.h>
#include <MaterialXGenShader2/Export.h>

#include <MaterialXGenShader/ShaderGenerator.h>
#include <MaterialXGenShader/ShaderGraph.h>

MATERIALX_NAMESPACE_BEGIN

/// Forward declaration — full type only needed in ShaderGraph2.cpp.
class IShaderSource;

/// @class ShaderGraph2
/// ShaderGraph subclass that exposes the protected construction methods
/// needed by ShaderGraphBuilder.
class MX_GENSHADER2_API ShaderGraph2 : public ShaderGraph
{
  public:
    using ShaderGraph::ShaderGraph;

    /// Calls the protected ShaderGraph::finalize().
    void finalize2(GenContext& context) { finalize(context); }

    /// Calls the protected ShaderGraph::addInputSockets().
    void addInputSockets2(const InterfaceElement& elem, GenContext& context)
    {
        addInputSockets(elem, context);
    }

    /// Calls the protected ShaderGraph::addOutputSockets().
    void addOutputSockets2(const InterfaceElement& elem, GenContext& context)
    {
        addOutputSockets(elem, context);
    }

    /// Exposes the protected _classification field for ShaderGraphBuilder.
    void setClassification2(uint32_t classification) { _classification = classification; }

    /// Sets the protected _document field. Called by GenContextCreate::buildShader()
    /// to provide the document for HW geomProp insertion, without triggering
    /// getMxDocument() during buildGraph() (required by Phase 4c tests).
    void setDocument(ConstDocumentPtr doc) { _document = doc; }

    /// Exposes the protected simple createNode overload that creates a ShaderNode
    /// without adding defaultgeomprop nodes or running applyInputTransforms.
    /// This mirrors the call used in ShaderGraph::create() for the root node.
    ///
    /// @note The node is stored in _nodeMap keyed by its short *name* (not the
    ///       full element path).  This matches ShaderGraph::createNode(ConstNodePtr)
    ///       and is required so that ShaderGraph::optimize() can correctly erase
    ///       bypassed nodes via _nodeMap.erase(node->getName()).  Storing by path
    ///       would cause a key mismatch that makes _nodeMap.size() diverge from
    ///       _nodeOrder.size(), causing a null-pointer crash in topologicalSort().
    ShaderNode* createNode2(const string& name, ConstNodeDefPtr nodeDef, GenContext& context)
    {
        if (!nodeDef)
            throw ExceptionShaderGenError("Could not find a nodedef for node '" + name + "'");
        ShaderNodePtr newNode = ShaderNode::create(this, name, *nodeDef, context);
        _nodeMap[name] = newNode;
        _nodeOrder.push_back(newNode.get());
        return newNode.get();
    }

    /// Initializes an upstream ShaderNode from IShaderSource queries, replacing
    /// the ShaderGraph::createNode(ConstNodePtr) path for non-MX backends.
    ///
    /// Replicates:
    ///   1. ShaderNode::initialize() — value/path/unit/colorspace copying
    ///   2. Interface-name connections to graph input sockets
    ///   3. defaultgeomprop node creation (still uses ConstNodeDefPtr, which is
    ///      always available even for non-MX backends)
    ///   4. applyInputTransforms2()
    ///
    /// @note ShaderNodeImpl::setValues(const Node&, ...) is NOT called here
    ///       because it requires a ConstNodePtr.  The only known override is
    ///       HwImageNode::setValues (UDIM UV normalization), which needs the
    ///       MxNode bridge.  That code path must still go through createNode(ConstNodePtr).
    void initializeNode2(DataHandle nodeHandle, ShaderNode* shaderNode,
                         ConstNodeDefPtr nodeDef, const IShaderSource& source,
                         GenContext& context);

    /// Applies color and unit transform nodes to the inputs of a ShaderNode,
    /// driven entirely by IShaderSource queries (replaces the
    /// ShaderGraph::applyInputTransforms(ConstNodePtr, ...) overload).
    ///
    /// @note The parent-node chain adjustment for filename inputs with an
    ///       interfaceName (context.getParentNodes()) requires ConstNodePtr and
    ///       is therefore skipped here.  This is a known limitation for non-MX
    ///       backends.
    void applyInputTransforms2(DataHandle nodeHandle, ShaderNode* shaderNode,
                               const IShaderSource& source, GenContext& context);

    /// Creates graph input sockets from a NodeDef's declared inputs.
    /// Replicates addInputSockets(InterfaceElement&) for the NodeDef case.
    void addInputSocketsFromNodeDef3(DataHandle nodeDefHandle,
                                     const IShaderSource& source, GenContext& context);

    /// Creates graph input sockets from a NodeGraph's declared inputs.
    /// Used when the NodeGraph has no associated NodeDef (free-standing graph).
    void addInputSocketsFromNodeGraph3(DataHandle nodeGraphHandle,
                                       const IShaderSource& source, GenContext& context);

    /// Creates graph input sockets from a Node's active inputs.
    /// Used when a Document-level Output's connected node acts as the interface.
    void addInputSocketsFromNode3(DataHandle nodeHandle,
                                  const IShaderSource& source, GenContext& context);

    /// Replicates ShaderGraph::addDefaultGeomNode() using IShaderSource queries
    /// instead of _document->getNodeDef().
    /// Called from both initializeNode2 (for upstream nodes) and
    /// ShaderGraphBuilder::buildNodeRoot (for the root node).
    /// @note Requires source.getMxNodeDefByHandle() to return a valid NodeDef.
    void addDefaultGeomNode2(ShaderInput* input, const GeomPropDef& geomprop,
                              const IShaderSource& source, GenContext& context);

  private:
    /// Replicates ShaderGraph::populateUnitTransformMap() using IShaderSource
    /// port queries instead of a ValueElementPtr.
    void populateUnitTransformMap2(UnitSystemPtr unitSystem, ShaderPort* shaderPort,
                                   DataHandle portHandle, const IShaderSource& source,
                                   const string& globalTargetUnitSpace, bool asInput);

    /// Creates one input socket from a port DataHandle.
    /// Shared implementation used by all addInputSocketsFrom*3 variants.
    void addInputSocketFromPort3(DataHandle portHandle,
                                  const IShaderSource& source, GenContext& context);
};

using ShaderGraph2Ptr = shared_ptr<ShaderGraph2>;

MATERIALX_NAMESPACE_END

#endif // MATERIALX_GENSHADER2_SHADERGRAPH2_H
