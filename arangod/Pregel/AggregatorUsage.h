////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 ArangoDB GmbH, Cologne, Germany
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

#ifndef ARANGODB_PREGEL_AGGRGS_USAGE_H
#define ARANGODB_PREGEL_AGGRGS_USAGE_H 1

#include <velocypack/vpack.h>
#include <velocypack/velocypack-aliases.h>
#include <functional>
#include <map>

namespace arangodb {
namespace pregel {
  
struct IAlgorithm;
class Aggregator;

class AggregatorUsage {
  const IAlgorithm* _create;
  std::map<std::string, Aggregator*> _values;

 public:
  AggregatorUsage(const IAlgorithm* c) : _create(c) {}
  ~AggregatorUsage();
  void aggregate(std::string const& name, const void* valuePtr);
  const void* getAggregatedValue(std::string const& name) const;
  void resetValues();
  void aggregateValues(AggregatorUsage const& workerValues);
  void aggregateValues(VPackSlice workerValues);
  void serializeValues(VPackBuilder& b) const;
  size_t size();
};
}
}
#endif