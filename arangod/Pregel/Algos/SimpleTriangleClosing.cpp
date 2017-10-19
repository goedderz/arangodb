#include "SimpleTriangleClosing.h"
#include "Pregel/Aggregator.h"
#include "Pregel/GraphFormat.h"
#include "Pregel/Iterators.h"
#include "Pregel/MasterContext.h"
#include "Pregel/Utils.h"
#include "Pregel/VertexComputation.h"
#include "Pregel/Algos/SimpleTriangleClosing/SimpleTriangleClosingMessageFormat.h"

#include <vector>
#include <cstdio>

using namespace arangodb;
using namespace arangodb::pregel;
using namespace arangodb::pregel::algos;

SimpleTriangleClosing::SimpleTriangleClosing(VPackSlice const& params)
  : SimpleAlgorithm("SimpleTriangleClosing", params) {
}

MessageFormat< std::vector<std::string> >*
SimpleTriangleClosing::messageFormat() const {
  return new arangodb::pregel::SimpleTriangleClosingMessageFormat();
}

struct STCComputation : public VertexComputation<std::string, int, std::vector<std::string> > {
  STCComputation() {}

  void compute(MessageIterator< std::vector<std::string> > const& messages)
      override {
          /*
    if (globalSuperstep() == 0) {
      std::vector< std::string > message = new std::vector< std::string >();
      message.push_back(vertexData());
      sendMessageToAllNeighbours(message);
    } else if (globalSuperstep() == 1) {
      for(auto const& message : messages) {
        
      }
    }
    */


/*
public int compute(
    Vertex<IntWritable, IntArrayListWritable, NullWritable> vertex,
    Iterable<IntWritable> messages) throws IOException {
  if (getSuperstep() == 0) {
    // send list of this vertex's neighbors to all neighbors
    for (Edge<IntWritable, NullWritable> edge : vertex.getEdges()) {
      sendMessageToAllEdges(vertex, edge.getTargetVertexId());
    }
  } else {
    for (IntWritable message : messages) {
      final int current = (closeMap.get(message) == null) ?
        0 : closeMap.get(message) + 1;
      closeMap.put(message, current);
    }
    // make sure the result values are sorted and
    // packaged in an IntArrayListWritable for output
    Set<Pair> sortedResults = Sets.<Pair>newTreeSet();
    for (Map.Entry<IntWritable, Integer> entry : closeMap.entrySet()) {
      sortedResults.add(new Pair(entry.getKey(), entry.getValue()));
    }
    IntArrayListWritable
      outputList = new IntArrayListWritable();
    for (Pair pair : sortedResults) {
      if (pair.value > 0) {
        outputList.add(pair.key);
      } else {
        break;
      }
    }
    vertex.setValue(outputList);
  }
  vertex.voteToHalt();
}
*/
    // TODO
    voteHalt();

  }
};

VertexComputation<std::string, int, std::vector<std::string> >* SimpleTriangleClosing::createComputation(
    WorkerConfig const* config) const {
  return new STCComputation();
}
