////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
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
/// @author Kaveh Vahedipour
////////////////////////////////////////////////////////////////////////////////

#include "FailedLeader.h"

#include "Agency/Agent.h"
#include "Agency/Job.h"

#include <algorithm>
#include <vector>

using namespace arangodb::consensus;

FailedLeader::FailedLeader(Node const& snapshot, Agent* agent,
                           std::string const& jobId, std::string const& creator,
                           std::string const& agencyPrefix,
                           std::string const& database,
                           std::string const& collection,
                           std::string const& shard, std::string const& from,
                           std::string const& to)
    : Job(snapshot, agent, jobId, creator, agencyPrefix),
      _database(database),
      _collection(collection),
      _shard(shard),
      _from(from),
      _to(to) {}

FailedLeader::~FailedLeader() {}

void FailedLeader::run() {
  try {
    JOB_STATUS js = status();

    if (js == TODO) {
      start();
    } else if (js == NOTFOUND) {
      if (create()) {
        start();
      }
    }
  } catch (std::exception const& e) {
    LOG_TOPIC(DEBUG, Logger::AGENCY) << e.what() << " " << __FILE__ << __LINE__;
    finish("Shards/" + _shard, false, e.what());
  }
}

bool FailedLeader::create() {
  LOG_TOPIC(INFO, Logger::AGENCY)
      << "Handle failed Leader for " + _shard + " from " + _from + " to " + _to;

  std::string path = _agencyPrefix + toDoPrefix + _jobId;

  _jb = std::make_shared<Builder>();
  _jb->openArray();
  _jb->openObject();

  // FIXME: consider clones and distributeShardsLike
  // FIXME: determine toServer here or earlier or later?

  // Todo entry
  _jb->add(path, VPackValue(VPackValueType::Object));
  _jb->add("creator", VPackValue(_creator));
  _jb->add("type", VPackValue("failedLeader"));
  _jb->add("database", VPackValue(_database));
  _jb->add("collection", VPackValue(_collection));
  _jb->add("shard", VPackValue(_shard));
  _jb->add("fromServer", VPackValue(_from));
  _jb->add("toServer", VPackValue(_to));
  _jb->add("isLeader", VPackValue(true));
  _jb->add("jobId", VPackValue(_jobId));
  _jb->add("timeCreated",
           VPackValue(timepointToString(std::chrono::system_clock::now())));
  _jb->close();

  // FIXME: is this FailedServers array containing shards still needed?

  // Add shard to /arango/Target/FailedServers/<server> array
  path = _agencyPrefix + failedServersPrefix + "/" + _from;
  _jb->add(path, VPackValue(VPackValueType::Object));
  _jb->add("op", VPackValue("push"));
  _jb->add("new", VPackValue(_shard));
  _jb->close();
  
  _jb->close();
  _jb->close();

  write_ret_t res = transact(_agent, *_jb);

  if (res.accepted && res.indices.size() == 1 && res.indices[0]) {
    return true;
  }

  LOG_TOPIC(INFO, Logger::AGENCY) << "Failed to insert job " + _jobId;
  return false;
}

bool FailedLeader::start() {
  // DBservers
  std::string planPath =
      planColPrefix + _database + "/" + _collection + "/shards/" + _shard;
  std::string curPath =
      curColPrefix + _database + "/" + _collection + "/" + _shard + "/servers";

  auto const& current = _snapshot(curPath).slice();
  auto const& planned = _snapshot(planPath).slice();

  // FIXME: this seems only to check whether there is an in sync follower,
  // FIXME: and not whether the toServer is in sync???

  if (current.length() == 1) {
    LOG_TOPIC(ERR, Logger::AGENCY)
      << "Failed to change leadership for shard " + _shard + " from " + _from 
      +  " to " + _to + ". No in-sync followers:" + current.toJson();
    return false;
  }

  // FIXME: abort lower priority jobs here to solve locks of shards or server

  // Copy todo to pending
  Builder todo, pending;

  // Get todo entry
  todo.openArray();
  if (_jb == nullptr) {
    try {
      _snapshot(toDoPrefix + _jobId).toBuilder(todo);
    } catch (std::exception const&) {
      LOG_TOPIC(INFO, Logger::AGENCY)
        << "Failed to get key " + toDoPrefix + _jobId + " from agency snapshot";
      return false;
    }
  } else {
    todo.add(_jb->slice().get(_agencyPrefix + toDoPrefix + _jobId).valueAt(0));
  }
  todo.close();
  
  // Transaction
  pending.openArray();

  // Apply
  // --- Add pending entry
  pending.openObject();
  pending.add(_agencyPrefix + pendingPrefix + _jobId,
              VPackValue(VPackValueType::Object));
  pending.add("timeStarted",
              VPackValue(timepointToString(std::chrono::system_clock::now())));
  for (auto const& obj : VPackObjectIterator(todo.slice()[0])) {
    pending.add(obj.key.copyString(), obj.value);
  }
  pending.close();

  // --- Remove todo entry
  pending.add(_agencyPrefix + toDoPrefix + _jobId,
              VPackValue(VPackValueType::Object));
  pending.add("op", VPackValue("delete"));
  pending.close();

  // --- Cyclic shift in sync servers
  // 1. only proceed if any in sync
  // 2. find 1st in sync that is is not in failedservers
  // 3. put all in sync not failed up front
  // 4. put failed leader
  // 5. remaining in plan
  //    Distribute shards like to come!
  std::vector<std::string> planv;
  for (auto const& i : VPackArrayIterator(planned)) {
    auto s = i.copyString();
    if (s != _from && s != _to) {
      planv.push_back(i.copyString());
    }
  }

  pending.add(_agencyPrefix + planPath, VPackValue(VPackValueType::Array));

  // FIXME: this is slightly different from design since the plan change
  // FIXME: is done right away, the actual plan change is also slightly
  // FIXME: different from spec, what do we want? Change specs?

  pending.add(VPackValue(_to));
  for (auto const& i : VPackArrayIterator(current)) {
    std::string s = i.copyString();
    if (s != _from && s != _to) {
      pending.add(i);
      planv.erase(std::remove(planv.begin(), planv.end(), s), planv.end());
    }
  }
  
  pending.add(VPackValue(_from));
  for (auto const& i : planv) {
    pending.add(VPackValue(i));
  }

  pending.close();

  // --- Block shard
  pending.add(_agencyPrefix + blockedShardsPrefix + _shard,
              VPackValue(VPackValueType::Object));
  pending.add("jobId", VPackValue(_jobId));
  pending.close();

  // --- Increment Plan/Version
  pending.add(_agencyPrefix + planVersion, VPackValue(VPackValueType::Object));
  pending.add("op", VPackValue("increment"));
  pending.close();

  pending.close();

  // Preconditions
  pending.openObject();

  // FIXME: If we insist on Current servers still being the same we have
  // FIXME: to allow for the fact that the toServer has been removed from
  // FIXME: Current in the meantime. This means that the decision as to
  // FIXME: what the toServer is has to be in the start() routine as
  // FIXME: specified

  // --- Check that Current servers are as we expect
  pending.add(_agencyPrefix + curPath, VPackValue(VPackValueType::Object));
  pending.add("old", current);
  pending.close();

  // --- Check that Current servers are as we expect
  pending.add(_agencyPrefix + planPath, VPackValue(VPackValueType::Object));
  pending.add("old", planned);
  pending.close();

  // --- Check if shard is not blocked
  pending.add(_agencyPrefix + blockedShardsPrefix + _shard,
              VPackValue(VPackValueType::Object));
  pending.add("oldEmpty", VPackValue(true));
  pending.close();

  // FIXME: also check that toServer is not locked in precondition
 
  pending.close();

  // Preconditions end
  pending.close();

  // Transact
  write_ret_t res = transact(_agent, pending);

  if (res.accepted && res.indices.size() == 1 && res.indices[0]) {
    LOG_TOPIC(DEBUG, Logger::AGENCY)
      << "Pending: Change leadership " + _shard + " from " + _from + " to " + _to;
    return true;
  }
  
  LOG_TOPIC(INFO, Logger::AGENCY)
      << "Precondition failed for starting job " + _jobId;
  return false;
}

JOB_STATUS FailedLeader::status() {
  auto status = exists();

  if (status != NOTFOUND) {  // Get job details from agency

    try {
      _database = _snapshot(pos[status] + _jobId + "/database").getString();
      _collection = _snapshot(pos[status] + _jobId + "/collection").getString();
      _from = _snapshot(pos[status] + _jobId + "/fromServer").getString();
      _to = _snapshot(pos[status] + _jobId + "/toServer").getString();
      _shard = _snapshot(pos[status] + _jobId + "/shard").getString();
    } catch (std::exception const& e) {
      std::stringstream err;
      err << "Failed to find job " << _jobId << " in agency: " << e.what();
      LOG_TOPIC(ERR, Logger::AGENCY) << err.str();
      finish("Shards/" + _shard, false, err.str());
      return FAILED;
    }
  }

  if (status == PENDING) {
    Node const& job = _snapshot(pendingPrefix + _jobId);
    std::string database = job("database").toJson(),
                collection = job("collection").toJson(),
                shard = job("shard").toJson();

    std::string planPath = planColPrefix + database + "/" + collection +
                           "/shards/" + shard,
                curPath = curColPrefix + database + "/" + collection + "/" +
                          shard + "/servers";

    Node const& planned = _snapshot(planPath);
    Node const& current = _snapshot(curPath);

    if (planned.slice()[0] == current.slice()[0]) {

      // FIXME: is this array still needed?

      // Remove shard to /arango/Target/FailedServers/<server> array
      Builder del;
      del.openArray();
      del.openObject();
      std::string path = _agencyPrefix + failedServersPrefix + "/" + _from;
      del.add(path, VPackValue(VPackValueType::Object));
      del.add("op", VPackValue("erase"));
      del.add("val", VPackValue(_shard));
      del.close();
      del.close();
      del.close();
      write_ret_t res = transact(_agent, del);
  
      if (finish("Shards/" + shard)) {
        return FINISHED;
      }
    }
    // FIXME: implement timeout check and rowing back
  }

  return status;
}

void FailedLeader::abort() {
  // TO BE IMPLEMENTED
}

