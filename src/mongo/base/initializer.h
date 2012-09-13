/*    Copyright 2012 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <string>
#include <vector>

#include "mongo/base/configuration_variable_manager.h"
#include "mongo/base/disallow_copying.h"
#include "mongo/base/initializer_context.h"
#include "mongo/base/initializer_dependency_graph.h"
#include "mongo/base/status.h"

namespace mongo {

    /**
     * Class representing an initialization process.
     *
     * Such a process is described by a directed acyclic graph of initialization operations, the
     * InitializerDependencyGraph, and a collection of mutable global state, the
     * ConfigurationVariableManager.  One constructs an initialization process by adding nodes and
     * edges to the graph, and variable mappings in the variable manager.  Then, one executes the
     * process, causing each initialization operation to execute in an order that respects the
     * programmer-established prerequistes.
     */
    class Initializer {
        MONGO_DISALLOW_COPYING(Initializer);
    public:
        Initializer();
        ~Initializer();

        /**
         * Get the initializer dependency graph, presumably for the purpose of adding more nodes.
         */
        InitializerDependencyGraph& getInitializerDependencyGraph() { return _graph; }

        /**
         * Get the configuration variable manager, for the purpose of describing more configurable
         * variables.
         */
        ConfigurationVariableManager& getConfigurationVariableManager() { return _configVariables; }

        /**
         * Execute the initializer process, using the given argv and environment data as input.
         *
         * Returns Status::OK on success.  All other returns constitute initialization failures,
         * and the thing being initialized should be considered dead in the water.
         */
        Status execute(const InitializerContext::ArgumentVector& args,
                       const InitializerContext::EnvironmentMap& env) const;

    private:

        InitializerDependencyGraph _graph;
        ConfigurationVariableManager _configVariables;
    };

}  // namespace mongo
