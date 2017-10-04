////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2017 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#include "GlobalInitialSyncer.h"
#include "Basics/Result.h"
#include "Basics/StringUtils.h"
#include "Basics/VelocyPackHelper.h"
#include "Replication/DatabaseInitialSyncer.h"
#include "RestServer/DatabaseFeature.h"
#include "SimpleHttpClient/SimpleHttpClient.h"
#include "SimpleHttpClient/SimpleHttpResult.h"
#include "VocBase/voc-types.h"
#include "VocBase/vocbase.h"
#include "VocBase/Methods/Databases.h"

#include <velocypack/Builder.h>
#include <velocypack/Iterator.h>
#include <velocypack/Slice.h>
#include <velocypack/velocypack-aliases.h>

using namespace arangodb;
using namespace arangodb::basics;
using namespace arangodb::httpclient;
using namespace arangodb::rest;

GlobalInitialSyncer::GlobalInitialSyncer(
  ReplicationApplierConfiguration const* configuration,
  std::unordered_map<std::string, bool> const& restrictCollections,
  Syncer::RestrictType restrictType, bool verbose, bool skipCreateDrop)
    : InitialSyncer(configuration, restrictCollections, restrictType, verbose, skipCreateDrop) {}

GlobalInitialSyncer::~GlobalInitialSyncer() {
  try {
    sendFinishBatch();
  } catch(...) {}
}

/// @brief run method, performs a full synchronization
Result GlobalInitialSyncer::run(bool incremental) {
  if (_client == nullptr || _connection == nullptr || _endpoint == nullptr) {
    return Result(TRI_ERROR_INTERNAL, "invalid endpoint");
  }
  
  setProgress("fetching master state");
  
  std::string errorMsg;
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "client: getting master state";
  int res = getMasterState(errorMsg);
    
  LOG_TOPIC(DEBUG, Logger::REPLICATION)
      << "client: got master state: " << res << " " << errorMsg;
  
  if (res != TRI_ERROR_NO_ERROR) {
    if (errorMsg.empty()) {
      return Result(res);
    }
    return Result(res, errorMsg);
  }
  
  if (_masterInfo._majorVersion > 3 ||
      (_masterInfo._majorVersion == 3 && _masterInfo._minorVersion < 3)) {
    char const* msg = "global replication is not supported with a master <  ArangoDB 3.3";
    LOG_TOPIC(WARN, Logger::REPLICATION) << msg;
    return Result(TRI_ERROR_INTERNAL, msg);
  }
  
  // create a WAL logfile barrier that prevents WAL logfile collection
  res = sendCreateBarrier(errorMsg, _masterInfo._lastLogTick);
  if (res != TRI_ERROR_NO_ERROR) {
    return Result(res, errorMsg);
  }
    
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "created logfile barrier";
  TRI_DEFER(sendRemoveBarrier());

  // start batch is required for the inventory request
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "sending start batch";
  res = sendStartBatch(errorMsg);
  if (res != TRI_ERROR_NO_ERROR) {
    return Result(res, errorMsg);
  }
  TRI_DEFER(sendFinishBatch());
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "sending start batch done";
  
  VPackBuilder builder;
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "fetching inventory";
  res = fetchInventory(builder, errorMsg);
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "inventory done: " << res;
  if (res != TRI_ERROR_NO_ERROR) {
    return Result(res, errorMsg);
  }
  
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "inventory: " << builder.slice().toJson();
  VPackSlice const databases = builder.slice().get("databases");
  VPackSlice const state = builder.slice().get("state");
  if (!databases.isObject() || !state.isObject()) {
    return Result(TRI_ERROR_REPLICATION_INVALID_RESPONSE,
                  "database section or state section is missing from response or is invalid");
  }
 
  if (!_skipCreateDrop) {
    LOG_TOPIC(DEBUG, Logger::REPLICATION) << "updating server inventory"; 
    Result r = updateServerInventory(databases);
    if (r.fail()) {
      LOG_TOPIC(DEBUG, Logger::REPLICATION) << "updating server inventory failed"; 
      return r;
    }
  }
      
  LOG_TOPIC(DEBUG, Logger::REPLICATION) << "databases: " << databases.toJson();
  
  try {
    // actually sync the database
    for (auto const& database : VPackObjectIterator(databases)) {
      VPackSlice it = database.value;
      if (!it.isObject()) {
        return Result(TRI_ERROR_REPLICATION_INVALID_RESPONSE,
                      "database declaration is invalid in response");
      }
      
      VPackSlice const nameSlice = it.get("name");
      VPackSlice const idSlice = it.get("id");
      VPackSlice const collections = it.get("collections");
      if (!nameSlice.isString() ||
          !idSlice.isString() ||
          !collections.isArray()) {
        return Result(TRI_ERROR_REPLICATION_INVALID_RESPONSE,
                      "database declaration is invalid in response");
      }
      
      TRI_vocbase_t* vocbase = resolveVocbase(nameSlice);
      TRI_ASSERT(vocbase != nullptr);
      if (vocbase == nullptr) {
        return Result(TRI_ERROR_INTERNAL, "vocbase not found");
      }

      // change database name in place
      std::string const oldName = _configuration._database;
      _configuration._database = nameSlice.copyString();
      TRI_DEFER(_configuration._database = oldName);
      DatabaseInitialSyncer syncer(vocbase, &_configuration, _restrictCollections,
                                   _restrictType, _verbose, _skipCreateDrop);
      
      syncer.useAsChildSyncer(_masterInfo, _barrierId, _barrierUpdateTime,
                              _batchId, _batchUpdateTime);
      // run the syncer with the supplied inventory collections
      Result r = syncer.runWithInventory(false, collections);
      if (r.fail()) {
        return r;
      }
    
      // we need to pass on the update times to the next syncer
      _barrierUpdateTime = syncer.barrierUpdateTime();
      _batchUpdateTime = syncer.batchUpdateTime();
    
      sendExtendBatch();
      sendExtendBarrier();
    }
  
  } catch (...) {
    return Result(TRI_ERROR_INTERNAL, "caught an unexpected exception");
  }
  
  return TRI_ERROR_NO_ERROR;
}

/// @brief add or remove databases such that the local inventory mirrors the masters
Result GlobalInitialSyncer::updateServerInventory(VPackSlice const& masterDatabases) {
  std::set<std::string> existingDBs;
  DatabaseFeature::DATABASE->enumerateDatabases([&](TRI_vocbase_t* vocbase) {
    existingDBs.insert(vocbase->name());
  });
  
  for (auto const& database : VPackObjectIterator(masterDatabases)) {
    VPackSlice it = database.value;

    if (!it.isObject()) {
      return Result(TRI_ERROR_REPLICATION_INVALID_RESPONSE,
                    "database declaration is invalid in response");
    }
    
    VPackSlice const nameSlice = it.get("name");
    VPackSlice const idSlice = it.get("id");
    VPackSlice const collections = it.get("collections");
    if (!nameSlice.isString() ||
        !idSlice.isString() ||
        !collections.isArray()) {
      return Result(TRI_ERROR_REPLICATION_INVALID_RESPONSE,
                    "database declaration is invalid in response");
    }
    std::string const dbName = nameSlice.copyString();
    TRI_vocbase_t* vocbase = resolveVocbase(nameSlice);
    if (vocbase == nullptr) {
      // database is missing we need to create it now
      
      Result r = methods::Databases::create(dbName, VPackSlice::emptyArraySlice(),
                                            VPackSlice::emptyObjectSlice());
      if (r.fail()) {
        LOG_TOPIC(WARN, Logger::REPLICATION) << "Creating the db failed on replicant";
        return r;
      }
      vocbase = resolveVocbase(nameSlice);
      TRI_ASSERT(vocbase != nullptr); // must be loaded now
      if (vocbase == nullptr) {
        char const* msg = "DB was created with wrong id on replicant";
        LOG_TOPIC(WARN, Logger::REPLICATION) << msg;
        return Result(TRI_ERROR_INTERNAL, msg);
      }
    }
    existingDBs.erase(dbName); // remove dbs that exists on the master
    
    sendExtendBatch();
    sendExtendBarrier();
  }
  
  // all dbs left in this list no longer exist on the master
  for (std::string const& dbname : existingDBs) {
    _vocbases.erase(dbname); // make sure to release the db first
    
    TRI_vocbase_t* system = DatabaseFeature::DATABASE->systemDatabase();
    Result r = methods::Databases::drop(system, dbname);
    if (r.fail()) {
      LOG_TOPIC(WARN, Logger::REPLICATION) << "Dropping db failed on replicant";
      return r;
    }
    
    sendExtendBatch();
    sendExtendBarrier();
  }
  
  return TRI_ERROR_NO_ERROR;
}

/*
void GlobalInitialSyncer::setProgress(std::string const& msg) {
  _progress = msg;
  
  if (_verbose) {
    LOG_TOPIC(INFO, Logger::REPLICATION) << msg;
  } else {
    LOG_TOPIC(DEBUG, Logger::REPLICATION) << msg;
  }
  
  TRI_replication_applier_t* applier = vocbase()->replicationApplier();
  if (applier != nullptr) {
    applier->setProgress(msg.c_str(), true);
  }
}*/

int GlobalInitialSyncer::fetchInventory(VPackBuilder& builder,
                                        std::string& errorMsg) {
  
  std::string url = BaseUrl + "/inventory?serverId=" + _localServerIdString +
  "&batchId=" + std::to_string(_batchId) + "&global=true";
  if (_includeSystem) {
    url += "&includeSystem=true";
  }
  
  // send request
  std::string const progress = "fetching master inventory from " + url;
  setProgress(progress);
  
  std::unique_ptr<SimpleHttpResult> response(
                                             _client->retryRequest(rest::RequestType::GET, url, nullptr, 0));
  
  if (response == nullptr || !response->isComplete()) {
    errorMsg = "could not connect to master at " + _masterInfo._endpoint +
    ": " + _client->getErrorMessage();
    
    sendFinishBatch();
    
    return TRI_ERROR_REPLICATION_NO_RESPONSE;
  }
  
  TRI_ASSERT(response != nullptr);
  
  if (response->wasHttpError()) {
    errorMsg = "got invalid response from master at " +
    _masterInfo._endpoint + ": HTTP " +
    StringUtils::itoa(response->getHttpReturnCode()) + ": " +
    response->getHttpReturnMessage();
    return TRI_ERROR_REPLICATION_MASTER_ERROR;
  }
  int res = parseResponse(builder, response.get());
  if (res != TRI_ERROR_NO_ERROR) {
    errorMsg = "got invalid response from master at " +
    std::string(_masterInfo._endpoint) +
    ": invalid response type for initial data. expecting array";
    
    return res;
  }
  
  VPackSlice const slice = builder.slice();
  if (!slice.isObject()) {
    LOG_TOPIC(DEBUG, Logger::REPLICATION) << "client: InitialSyncer::run - "
    "inventoryResponse is not an "
    "object";
    
    errorMsg = "got invalid response from master at " +
    _masterInfo._endpoint + ": invalid JSON";
    return TRI_ERROR_REPLICATION_INVALID_RESPONSE;
  }
  return res;
}

