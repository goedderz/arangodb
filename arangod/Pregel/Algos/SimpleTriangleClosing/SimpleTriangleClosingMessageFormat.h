#ifndef ARANGODB_PREGEL_ALGO_SIMPLETRIANGLECLOSING_MESSAGE_F_H
#define ARANGODB_PREGEL_ALGO_SIMPLETRIANGLECLOSING_MESSAGE_F_H 1

#include <velocypack/Builder.h>
#include <velocypack/Slice.h>

#include "Pregel/Graph.h"
#include "Pregel/GraphFormat.h"
#include "Pregel/MessageFormat.h"

namespace arangodb {
namespace pregel {

struct SimpleTriangleClosingMessageFormat
  : public MessageFormat< std::vector<std::string> > {
  SimpleTriangleClosingMessageFormat() {}

  void unwrapValue(VPackSlice s, std::vector<std::string>& message) const override {
    VPackArrayIterator array(s);
    for(auto const &vertexId : array) {
      message.push_back(vertexId.copyString());
    }
  }

  void addValue(VPackBuilder& arrayBuilder,
                std::vector<std::string> const& message) const override {
    arrayBuilder.add(VPackValue(VPackValueType::Array));
    for(auto const &vertexId : message) {
      arrayBuilder.add(VPackValue(vertexId));
    }
    arrayBuilder.close();
  }

};


}
}
#endif
