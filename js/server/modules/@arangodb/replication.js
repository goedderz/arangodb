/* global ArangoServerState, GLOBAL_REPLICATION_APPLIER_START, GLOBAL_REPLICATION_APPLIER_SHUTDOWN, GLOBAL_REPLICATION_APPLIER_STATE, GLOBAL_REPLICATION_APPLIER_FORGET, GLOBAL_REPLICATION_APPLIER_CONFIGURE , REPLICATION_APPLIER_START, REPLICATION_APPLIER_SHUTDOWN, REPLICATION_APPLIER_STATE, REPLICATION_APPLIER_FORGET, REPLICATION_APPLIER_CONFIGURE */
'use strict';

// //////////////////////////////////////////////////////////////////////////////
// / @brief Replication management
// /
// / @file
// /
// / DISCLAIMER
// /
// / Copyright 2012 triagens GmbH, Cologne, Germany
// /
// / Licensed under the Apache License, Version 2.0 (the "License")
// / you may not use this file except in compliance with the License.
// / You may obtain a copy of the License at
// /
// /     http://www.apache.org/licenses/LICENSE-2.0
// /
// / Unless required by applicable law or agreed to in writing, software
// / distributed under the License is distributed on an "AS IS" BASIS,
// / WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// / See the License for the specific language governing permissions and
// / limitations under the License.
// /
// / Copyright holder is triAGENS GmbH, Cologne, Germany
// /
// / @author Jan Steemann
// / @author Copyright 2013, triAGENS GmbH, Cologne, Germany
// //////////////////////////////////////////////////////////////////////////////

var internal = require('internal');
var ERRORS = internal.errors;
var endpointToURL = require('@arangodb/cluster').endpointToURL;
var request;
if (ArangoServerState.role() === 'PRIMARY') {
  request = require('@arangodb/request').clusterRequest;
} else {
  request = require('@arangodb/request').request;
}

let logger = { };
let applier = { };
let globalApplier = { };

// / @brief return the replication logger state
logger.state = function () {
  return internal.getStateReplicationLogger();
};

// / @brief return the tick ranges provided by the replication logger
logger.tickRanges = function () {
  return internal.tickRangesReplicationLogger();
};

// / @brief return the first tick that can be provided by the replication logger
logger.firstTick = function () {
  return internal.firstTickReplicationLogger();
};

// / @brief starts the replication applier
applier.start = function (initialTick, barrierId) {
  if (initialTick === undefined) {
    return REPLICATION_APPLIER_START();
  }

  return REPLICATION_APPLIER_START(initialTick, barrierId);
};

// / @brief shuts down the replication applier
applier.shutdown = applier.stop = function () { return REPLICATION_APPLIER_SHUTDOWN(); };

// / @brief return the replication applier state
applier.state = function () { return REPLICATION_APPLIER_STATE(); };

// / @brief stop the applier and "forget" all configuration
applier.forget = function () { return REPLICATION_APPLIER_FORGET(); };

// / @brief returns the configuration of the replication applier
applier.properties = function (config) {
  if (config === undefined) {
    return REPLICATION_APPLIER_CONFIGURE();
  }

  return REPLICATION_APPLIER_CONFIGURE(config);
};

// / @brief starts the global replication applier
globalApplier.start = function (initialTick, barrierId) {
  if (initialTick === undefined) {
    return GLOBAL_REPLICATION_APPLIER_START();
  }

  return GLOBAL_REPLICATION_APPLIER_START(initialTick, barrierId);
};

// / @brief shuts down the global replication applier
globalApplier.shutdown = globalApplier.stop = function () { return GLOBAL_REPLICATION_APPLIER_SHUTDOWN(); };

// / @brief return the global replication applier state
globalApplier.state = function () { return GLOBAL_REPLICATION_APPLIER_STATE(); };

// / @brief stop the global applier and "forget" all configuration
globalApplier.forget = function () { return GLOBAL_REPLICATION_APPLIER_FORGET(); };

// / @brief returns the configuration of the global replication applier
globalApplier.properties = function (config) {
  if (config === undefined) {
    return GLOBAL_REPLICATION_APPLIER_CONFIGURE();
  }

  return GLOBAL_REPLICATION_APPLIER_CONFIGURE(config);
};

// / @brief performs a one-time synchronization with a remote endpoint
function sync (config) {
  return internal.synchronizeReplication(config);
}

// / @brief performs a one-time synchronization with a remote endpoint
function syncCollection (collection, config) {
  config = config || { };
  config.restrictType = 'include';
  config.restrictCollections = [ collection ];
  config.includeSystem = true;
  if (!config.hasOwnProperty('verbose')) {
    config.verbose = false;
  }

  return internal.synchronizeReplication(config);
}

// / @brief sets up the replication (all-in-one function for initial
// / synchronization and continuous replication)
function setupReplication (config) {
  config = config || { };
  if (!config.hasOwnProperty('autoStart')) {
    config.autoStart = true;
  }
  if (!config.hasOwnProperty('includeSystem')) {
    config.includeSystem = true;
  }
  if (!config.hasOwnProperty('verbose')) {
    config.verbose = false;
  }
  config.keepBarrier = true;

  try {
    // stop previous instance
    applier.stop();
  } catch (err) {}
  // remove existing configuration
  applier.forget();

  // run initial sync
  var result = internal.synchronizeReplication(config);

  // store applier configuration
  applier.properties(config);

  applier.start(result.lastLogTick, result.barrierId);
  return applier.state();
}

// / @brief returns the server's id
function serverId () {
  return internal.serverId();
}

exports.logger = logger;
exports.applier = applier;
exports.globalApplier = globalApplier;
exports.sync = sync;
exports.syncCollection = syncCollection;
exports.setupReplication = setupReplication;
exports.serverId = serverId;
