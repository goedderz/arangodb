#ifndef ARANGODB_PREGEL_ALGO_SIMPLETRIANGLECLOSING_GRAPH_F_H
#define ARANGODB_PREGEL_ALGO_SIMPLETRIANGLECLOSING_GRAPH_F_H 1

#include <velocypack/Builder.h>
#include <velocypack/Slice.h>

#include "Pregel/Graph.h"
#include "Pregel/GraphFormat.h"

namespace arangodb {
namespace pregel {

class SimpleTriangleClosingGraphFormat : public GraphFormat<std::string, int> {
 protected:
  const std::string _resultField;
  const std::string _vDefault;

 public:
  SimpleTriangleClosingGraphFormat(std::string const& result, std::string vertexNull)
      : _resultField(result), _vDefault(vertexNull) {}

  size_t estimatedVertexSize() const override { return sizeof(std::string); };
  size_t estimatedEdgeSize() const override { return 0; };

  size_t copyVertexData(std::string const& documentId,
                        arangodb::velocypack::Slice document, std::string* targetPtr,
                        size_t maxSize) override {
    *targetPtr = documentId;
    return sizeof(std::string);
  }

  size_t copyEdgeData(arangodb::velocypack::Slice document, int* targetPtr,
                      size_t maxSize) override {
    return 0;
  }

  bool buildVertexDocument(arangodb::velocypack::Builder& b, const std::string* ptr,
                           size_t size) const override {
    b.add(_resultField, arangodb::velocypack::Value(*ptr));
    return true;
  }

  bool buildEdgeDocument(arangodb::velocypack::Builder& b, const int* ptr,
                         size_t size) const override {
    return false;
  }
};

}
}
#endif
