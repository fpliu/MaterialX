//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MATERIALX_GENCONTEXT_H
#define MATERIALX_GENCONTEXT_H

/// @file
/// Context classes for shader generation

#include <MaterialXGenShader/Export.h>

#include <MaterialXGenShader/GenOptions.h>
#include <MaterialXGenShader/GenUserData.h>
#include <MaterialXGenShader/ShaderNode.h>

#include <MaterialXFormat/File.h>

MATERIALX_NAMESPACE_BEGIN

class ClosureContext;

/// A standard function to allow for handling of application variables for a given node
using ApplicationVariableHandler = std::function<void(ShaderNode*, GenContext&)>;

/// @class GenContext
/// A context class for shader generation.
/// Used for thread local storage of data needed during shader generation.
class MX_GENSHADER_API GenContext
{
  public:
    /// Constructor.
    GenContext(ShaderGeneratorPtr sg);

    /// Return shader generatior.
    ShaderGenerator& getShaderGenerator()
    {
        return *_sg;
    }

    /// Return shader generation options.
    GenOptions& getOptions()
    {
        return _options;
    }

    /// Return shader generation options.
    const GenOptions& getOptions() const
    {
        return _options;
    }

    /// Register a user search path for finding source code during
    /// code generation.
    void registerSourceCodeSearchPath(const FilePath& path)
    {
        _sourceCodeSearchPath.append(path);
    }

    /// Register a user search path for finding source code during
    /// code generation.
    void registerSourceCodeSearchPath(const FileSearchPath& path)
    {
        _sourceCodeSearchPath.append(path);
    }

    /// Resolve a source code filename, first checking the given local path
    /// then checking any file paths registered by the user.
    FilePath resolveSourceFile(const FilePath& filename, const FilePath& localPath) const
    {
        FileSearchPath searchPath = _sourceCodeSearchPath;
        if (!localPath.isEmpty())
        {
            searchPath.prepend(localPath);
        }
        return searchPath.find(filename).getNormalized();
    }

    /// Add reserved words that should not be used as
    /// identifiers during code generation.
    void addReservedWords(const StringSet& names)
    {
        _reservedWords.insert(names.begin(), names.end());
    }

    /// Return the set of reserved words that should not be used
    /// as identifiers during code generation.
    const StringSet& getReservedWords() const
    {
        return _reservedWords;
    }

    /// Cache a shader node implementation.
    void addNodeImplementation(const string& name, ShaderNodeImplPtr impl);

    /// Find and return a cached shader node implementation,
    /// or return nullptr if no implementation is found.
    ShaderNodeImplPtr findNodeImplementation(const string& name) const;

    /// Get the names of all cached node implementations.
    void getNodeImplementationNames(StringSet& names);

    /// Clear all cached shader node implementation.
    void clearNodeImplementations();

    /// Push a new closure context to use for closure evaluation.
    void pushClosureContext(ClosureContext* cct)
    {
        _closureContexts.push_back(cct);
    }

    /// Pop the current closure context.
    void popClosureContext()
    {
        if (_closureContexts.size())
        {
            _closureContexts.pop_back();
        }
    }

    /// Return the current closure context.
    ClosureContext* getClosureContext()
    {
        return _closureContexts.size() ? _closureContexts.back() : nullptr;
    }

    /// Push a parent node onto the stack
    void pushParentNode(ConstNodePtr node)
    {
        _parentNodes.push_back(node);
    }

    /// Pop the current parent node from the stack.
    void popParentNode()
    {
        _parentNodes.pop_back();
    }

    /// Return the current stack of parent nodes.
    const vector<ConstNodePtr>& getParentNodes()
    {
        return _parentNodes;
    }

    /// Add user data to the context to make it
    /// available during shader generator.
    void pushUserData(const string& name, GenUserDataPtr data)
    {
        auto it = _userData.find(name);
        if (it != _userData.end())
        {
            it->second.push_back(data);
        }
        else
        {
            _userData[name] = { data };
        }
    }

    /// Remove user data from the context.
    void popUserData(const string& name)
    {
        auto it = _userData.find(name);
        if (it != _userData.end())
        {
            it->second.pop_back();
        }
    }

    /// Clear all user data from the context.
    void clearUserData();

    /// Return user data with given name,
    /// or nullptr if no data is found.
    template <class T>
    std::shared_ptr<T> getUserData(const string& name)
    {
        auto it = _userData.find(name);
        return it != _userData.end() && !it->second.empty() ? it->second.back()->asA<T>() : nullptr;
    }

    /// Add an input suffix to be used for the input in this context.
    /// @param input Node input
    /// @param suffix Suffix string
    void addInputSuffix(const ShaderInput* input, const string& suffix);

    /// Remove an input suffix to be used for the input in this context.
    /// @param input Node input
    void removeInputSuffix(const ShaderInput* input);

    /// Get an input suffix to be used for the input in this context.
    /// @param input Node input
    /// @param suffix Suffix string returned. Is empty if not found.
    void getInputSuffix(const ShaderInput* input, string& suffix) const;

    /// Add an output suffix to be used for the output in this context.
    /// @param output Node output
    /// @param suffix Suffix string
    void addOutputSuffix(const ShaderOutput* output, const string& suffix);

    /// Remove an output suffix to be used for the output in this context.
    /// @param output Node output
    void removeOutputSuffix(const ShaderOutput* output);

    /// Get an output suffix to be used for the output in this context.
    /// @param output Node output
    /// @param suffix Suffix string returned. Is empty if not found.
    void getOutputSuffix(const ShaderOutput* output, string& suffix) const;

    /// Set handler for application variables
    void setApplicationVariableHandler(ApplicationVariableHandler handler)
    {
        _applicationVariableHandler = handler;
    }

    /// Get handler for application variables
    ApplicationVariableHandler getApplicationVariableHandler() const
    {
        return _applicationVariableHandler;
    }

  protected:
    GenContext() = delete;

    ShaderGeneratorPtr _sg;
    GenOptions _options;
    FileSearchPath _sourceCodeSearchPath;
    StringSet _reservedWords;

    std::unordered_map<string, ShaderNodeImplPtr> _nodeImpls;
    std::unordered_map<string, vector<GenUserDataPtr>> _userData;
    std::unordered_map<const ShaderInput*, string> _inputSuffix;
    std::unordered_map<const ShaderOutput*, string> _outputSuffix;

    vector<ClosureContext*> _closureContexts;
    vector<ConstNodePtr> _parentNodes;

    ApplicationVariableHandler _applicationVariableHandler;
};

/// @class ClosureContext
/// Class representing a context for closure evaluation.
/// On hardware BSDF closures are evaluated differently in reflection, transmission
/// or environment/indirect contexts. This class represents the context we are in
/// and if extra arguments and function decorators are needed for that context.
class MX_GENSHADER_API ClosureContext
{
  public:
    /// An extra argument for closure functions.
    /// An argument is a pair of strings holding the
    /// 'type' and 'name' of the argument.
    using Argument = std::pair<TypeDesc, string>;
    /// An array of arguments
    using Arguments = vector<Argument>;

    /// Constructor
    ClosureContext() {}

    /// For the given node type add an extra argument to be used for the function in this context.
    void addArgument(const Argument& arg)
    {
        _arguments.push_back(arg);
    }

    /// Return a list of extra argument to be used for the given node in this context.
    const Arguments& getArguments() const
    {
        return _arguments;
    }

  protected:
    Arguments _arguments;

    static const Arguments EMPTY_ARGUMENTS;
};

/// A RAII class for overriding port variable names.
class MX_GENSHADER_API ScopedSetVariableName
{
  public:
    /// Constructor for setting a new variable name for a port.
    ScopedSetVariableName(const string& name, ShaderPort* port);

    /// Destructor restoring the original variable name.
    ~ScopedSetVariableName();

  private:
    ShaderPort* _port;
    string _oldName;
};

MATERIALX_NAMESPACE_END

#endif // MATERIALX_GENCONTEXT_H
