#ifndef ARANGODB_PREGEL_ALGOS_SIMPLETRIANGLECLOSING_H
#define ARANGODB_PREGEL_ALGOS_SIMPLETRIANGLECLOSING_H 1

#include <velocypack/Slice.h>
#include <vector>
#include "Pregel/Algorithm.h"
#include "Pregel/Algos/SimpleTriangleClosing/SimpleTriangleClosingGraphFormat.h"

namespace arangodb {
namespace pregel {
namespace algos {

struct SimpleTriangleClosing
    : public SimpleAlgorithm< std::string, int, std::vector<std::string> > {

  explicit SimpleTriangleClosing(arangodb::velocypack::Slice const& params);

  GraphFormat<std::string, int>* inputFormat() const override {
    return new SimpleTriangleClosingGraphFormat("", "");
  }

  MessageFormat< std::vector<std::string> >* messageFormat() const override;

  VertexComputation<std::string, int, std::vector<std::string> >* createComputation(
      WorkerConfig const*) const override;

};

}
}
}
#endif
