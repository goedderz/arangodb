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

#include "WalAccess.h"
#include "RestServer/DatabaseFeature.h"
#include "VocBase/LogicalCollection.h"

using namespace arangodb;

/// @brief Check if collection is in filter
bool WalAccessContext::shouldHandleCollection(TRI_voc_tick_t dbid, TRI_voc_cid_t cid) {
  TRI_ASSERT(dbid != 0 && cid != 0);
  if (_filter.vocbase == 0) { // tail everything
    return true;
  }
  return _filter.vocbase == dbid &&
  (_filter.collection == 0 || _filter.collection == cid);
}

/// @brief try to get collection, may return null
TRI_vocbase_t* WalAccessContext::loadVocbase(TRI_voc_tick_t dbid) {
  TRI_ASSERT(dbid != 0);
  auto const& it = _vocbases.find(dbid);
  if (it == _vocbases.end()) {
    TRI_vocbase_t* vocbase = DatabaseFeature::DATABASE->useDatabase(dbid);
    if (vocbase != nullptr) {
      TRI_DEFER(vocbase->release());
      _vocbases.emplace(dbid, DatabaseGuard(vocbase));
    }
    return vocbase;
  } else {
    return it->second.database();
  }
}

/// @brief get global unique id
std::string const& WalAccessContext::cidToUUID(TRI_voc_tick_t dbid, TRI_voc_cid_t cid) {
  auto const& uuid = _uuidCache.find(cid);
  if (uuid != _uuidCache.end()) {
    return uuid->second;
  }
  
  TRI_vocbase_t* vocbase = loadVocbase(dbid);
  LogicalCollection* collection = vocbase->lookupCollection(cid);
  _uuidCache.emplace(cid, collection->globallyUniqueId());
  return _uuidCache[cid];
}

/// @brief cid to collection name
std::string WalAccessContext::cidToName(TRI_voc_tick_t dbid, TRI_voc_cid_t cid) {
  TRI_vocbase_t* vocbase = loadVocbase(dbid);
  return vocbase->collectionName(cid);
}
