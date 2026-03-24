//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXGenShader2/GenContextCreate.h>
#include <MaterialXGenShader2/ShaderGraphBuilder.h>

#include <MaterialXGenShader/Exception.h>

MATERIALX_NAMESPACE_BEGIN

GenContextCreate::GenContextCreate(ShaderGeneratorPtr generator,
                                   std::unique_ptr<IShaderSource> source)
    : _source(std::move(source))
    , _genContext(generator)
{
}

ShaderGraph2Ptr GenContextCreate::buildGraph(const string& name)
{
    ShaderGraphBuilder builder(*_source, _genContext);
    return builder.build(name);
}

ShaderPtr GenContextCreate::buildShader(const string& name)
{
    ShaderGraph2Ptr graph = buildGraph(name);
    if (!graph)
    {
        throw ExceptionShaderGenError("GenContextCreate::buildShader: buildGraph returned nullptr for '" + name + "'.");
    }
    // Provide the document lazily — after buildGraph() — so that getMxDocument()
    // is not called during graph construction (required by Phase 4c tests).
    // HwShaderGenerator::createShader(ShaderGraphPtr) uses graph->getDocument() to
    // look up GeomPropDefs for HW geomProp insertion.
    graph->setDocument(_source->getMxDocument());
    return _genContext.getShaderGenerator().generate(name, graph, _genContext);
}

MATERIALX_NAMESPACE_END
