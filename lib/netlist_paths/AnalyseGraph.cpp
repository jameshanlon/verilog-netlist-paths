#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <boost/graph/reverse_graph.hpp>
#include "netlist_paths/AnalyseGraph.hpp"
#include "netlist_paths/Exception.hpp"
#include "netlist_paths/Options.hpp"
#include "netlist_paths/utilities.hpp"

namespace fs = boost::filesystem;
using namespace netlist_paths;

class DfsVisitor : public boost::default_dfs_visitor {
private:
  ParentMap &parentMap;
  bool allPaths;
public:
  DfsVisitor(ParentMap &parentMap, bool allPaths) :
      parentMap(parentMap), allPaths(allPaths) {}
  // Visit only the edges of the DFS graph.
  template<typename Edge, typename Graph>
  void tree_edge(Edge edge, const Graph &graph) const {
    if (!allPaths) {
      VertexDesc src, dst;
      src = boost::source(edge, graph);
      dst = boost::target(edge, graph);
      parentMap[dst].push_back(src);
    }
    return;
  }
  // Visit all edges of a all vertices.
  template<typename Edge, typename Graph>
  void examine_edge(Edge edge, const Graph &graph) const {
    if (allPaths) {
      VertexDesc src, dst;
      src = boost::source(edge, graph);
      dst = boost::target(edge, graph);
      parentMap[dst].push_back(src);
    }
    return;
  }
};

namespace boost {
  // Provide template specializations of lexical casts for Boost to convert
  // strings to/from enums in the dynamic property map.
  template<>
  netlist_paths::VertexType lexical_cast(const std::string &s) {
    return netlist_paths::getVertexType(s);
  }
  template<>
  netlist_paths::VertexDirection lexical_cast(const std::string &s) {
    return netlist_paths::getVertexDirection(s);
  }
  template<>
  std::string lexical_cast(const netlist_paths::VertexType &t) {
    return netlist_paths::getVertexTypeStr(t);
  }
  template<>
  std::string lexical_cast(const netlist_paths::VertexDirection &t) {
    return netlist_paths::getVertexDirectionStr(t);
  }
} // End boost namespace.

AnalyseGraph::AnalyseGraph() : dp(boost::ignore_other_properties) {
  // Initialise dynamic propery maps for the graph.
  dp.property("id",    boost::get(&VertexProperties::id,    graph));
  dp.property("type",  boost::get(&VertexProperties::type,  graph));
  dp.property("dir",   boost::get(&VertexProperties::dir,   graph));
  dp.property("name",  boost::get(&VertexProperties::name,  graph));
  dp.property("loc",   boost::get(&VertexProperties::loc,   graph));
  dp.property("isTop", boost::get(&VertexProperties::isTop, graph));
}

/// Parse a graph input file and return a list of Vertices and a list of Edges.
void AnalyseGraph::parseFile(const std::string &filename) {
  DEBUG(std::cout << "Parsing input file\n");
  std::fstream infile(filename);
  if (!infile.is_open()) {
    throw Exception("could not open file");
  }
  if (!boost::read_graphviz(infile, graph, dp)) {
    throw Exception(std::string("reading graph file: ")+filename);
  }
  // Perform some checks.
  BGL_FORALL_VERTICES(v, graph, Graph) {
    graph[v].isTop = netlist_paths::determineIsTop(graph[v].name);
    // Source registers don't have in edges.
    if (graph[v].type == VertexType::REG_SRC) {
      if (boost::in_degree(v, graph) > 0)
         std::cout << "Warning: source reg " << graph[v].name
                   << " (" << graph[v].id << ") has in edges" << "\n";
    }
    // Destination registers don't have out edges.
    if (graph[v].type == VertexType::REG_DST) {
      if (boost::out_degree(v, graph) > 0)
        std::cout << "Warning: destination reg " << graph[v].name
                  << " (" << graph[v].id << ") has out edges"<<"\n";
    }
    // NOTE: vertices may be incorrectly marked as reg if a field of a
    // structure has a delayed assignment to a field of it.
  }
}

/// Dump a Graphviz dotfile of the netlist graph for visualisation.
void AnalyseGraph::dumpDotFile(const std::string &outputFilename) const {
  std::ofstream outputFile(outputFilename);
  if (!outputFile.is_open()) {
    throw Exception(std::string("unable to open ")+outputFilename);
  }
  // Write graphviz format.
  boost::write_graphviz_dp(outputFile, graph, dp, /*node_id=*/"name");
  outputFile.close();
  // Print command line to generate graph file.
  DEBUG(std::cout << "dot -Tpdf " << outputFilename << " -o graph.pdf\n");
}

VertexDesc AnalyseGraph::getVertexDesc(const std::string &name,
                                       VertexType type) const {
  BGL_FORALL_VERTICES(v, graph, Graph) {
    if (graph[v].name == name &&
        graph[v].type == type) {
      DEBUG(std::cout<<"Vertex "<<graph[v].id<<" matches "<<name
                     <<" of type "<<getVertexTypeStr(type)<<"\n");
      return v;
    }
  }
  return boost::graph_traits<Graph>::null_vertex();
}

VertexDesc AnalyseGraph::getVertex(const std::string &name,
                                   const std::vector<VertexType> &types) const {
  for (auto &type : types) {
    auto v = getVertexDesc(name, type);
    if (v != boost::graph_traits<Graph>::null_vertex()) {
      return v;
    }
  }
  throw Exception(std::string("could not find vertex ")+name);
}

VertexDesc AnalyseGraph::getStartVertex(const std::string &name) const {
  auto types = {VertexType::REG_SRC,
                VertexType::VAR,
                VertexType::WIRE,
                VertexType::PORT};
  return getVertex(name, types);
}

VertexDesc AnalyseGraph::getEndVertex(const std::string &name) const {
  auto types = {VertexType::REG_DST,
                VertexType::VAR,
                VertexType::WIRE,
                VertexType::PORT};
  return getVertex(name, types);
}

VertexDesc AnalyseGraph::getMidVertex(const std::string &name) const {
  auto types = {VertexType::VAR,
                VertexType::WIRE,
                VertexType::PORT};
  return getVertex(name, types);
}

void AnalyseGraph::dumpPath(const std::vector<VertexDesc> &path) const {
  for (auto v : path) {
    if (!isLogic(graph[v].type)) {
      std::cout << "  " << graph[v].name << "\n";
    }
  }
}

/// Given the tree structure from a DFS, traverse the tree from leaf to root to
/// return a path.
Path AnalyseGraph::determinePath(ParentMap &parentMap,
                                 Path path,
                                 VertexDesc startVertex,
                                 VertexDesc endVertex) const {
  path.push_back(endVertex);
  if (endVertex == startVertex) {
    return path;
  }
  if (parentMap[endVertex].size() == 0)
    return std::vector<VertexDesc>();
  assert(parentMap[endVertex].size() == 1);
  auto nextVertex = parentMap[endVertex].front();
  assert(std::find(std::begin(path),
                   std::end(path),
                   nextVertex) == std::end(path));
  return determinePath(parentMap, path, startVertex, nextVertex);
}

/// Determine all paths between a start and an end point.
/// This performs a DFS starting at the end point. It is not feasible for large
/// graphs since the number of simple paths grows exponentially.
void AnalyseGraph::determineAllPaths(ParentMap &parentMap,
                                     std::vector<Path> &result,
                                     Path path,
                                     VertexDesc startVertex,
                                     VertexDesc endVertex) const {
  path.push_back(endVertex);
  if (endVertex == startVertex) {
    DEBUG(std::cout << "FOUND PATH\n");
    result.push_back(path);
    return;
  }
  DEBUG(std::cout<<"length "<<path.size()<<" vertex "<<graph[endVertex].id<<"\n");
  DEBUG(dumpPath(path));
  DEBUG(std::cout<<(parentMap[endVertex].empty()?"DEAD END\n":""));
  for (auto vertex : parentMap[endVertex]) {
    if (std::find(std::begin(path), std::end(path), vertex) == std::end(path)) {
      determineAllPaths(parentMap, result, path, startVertex, vertex);
    } else {
      DEBUG(std::cout << "CYCLE DETECTED\n");
    }
  }
}

/// Dump unique names of vars/regs/wires in the netlist for searching.
void AnalyseGraph::dumpVertexNames() const {
  std::unordered_set<std::string> names;
  BGL_FORALL_VERTICES(v, graph, Graph) {
    if (isLogic(graph[v].type))
      continue;
    if (graph[v].dir == VertexDirection::NONE) {
      auto name = std::string(getVertexTypeStr(graph[v].type))+" "
                    +graph[v].name;
      names.insert(name);
    } else {
      auto name = std::string(getVertexTypeStr(graph[v].type))+" "
                    +getVertexDirectionStr(graph[v].dir)+" "
                    +graph[v].name;
      names.insert(name);
    }
  }
  std::vector<std::string> sortedNames(names.begin(), names.end());
  std::sort(sortedNames.begin(), sortedNames.end());
  for (auto &name : sortedNames) {
    std::cout << std::setw(8) << name << "\n";
  }
}

/// Pretty print a path (some sequence of vertices).
void AnalyseGraph::printPathReport(const Path &path) const {
  // Determine the max length of a name.
  size_t maxNameLength = 0;
  for (auto v : path) {
    if (canIgnore(graph[v].name))
      continue;
    maxNameLength = std::max(maxNameLength, graph[v].name.size());
  }
  // Print each vertex on the path.
  for (auto v : path) {
    if (canIgnore(graph[v].name))
      continue;
    auto srcPath = options.filenamesOnly ? fs::path(graph[v].loc).filename()
                                         : fs::path(graph[v].loc);
    if (options.netsOnly) {
      if (!isLogic(graph[v].type)) {
        std::cout << "  " << std::left
                  << std::setw(maxNameLength+1) << graph[v].name
                  << srcPath.string() << "\n";
      }
    } else {
      if (isLogic(graph[v].type)) {
        std::cout << "  " << std::left
                  << std::setw(maxNameLength+1)
                  << getVertexTypeStr(graph[v].type)
                  << std::setw(VERTEX_TYPE_STR_MAX_LEN)
                  << "LOGIC"
                  << srcPath.string() << "\n";
      } else {
        std::cout << "  " << std::left
                  << std::setw(maxNameLength+1)
                  << graph[v].name
                  << std::setw(VERTEX_TYPE_STR_MAX_LEN)
                  << getVertexTypeStr(graph[v].type)
                  << srcPath.string() << "\n";
      }
    }
  }
}

/// Print a collection of paths.
void AnalyseGraph::
printPathReport(const std::vector<Path> &paths) const {
  int pathCount = 0;
  for (auto &path : paths) {
    if (!path.empty()) {
      std::cout << "Path " << ++pathCount << "\n";
      printPathReport(path);
      std::cout << "\n";
    }
  }
  std::cout << "Found " << pathCount << " path(s)\n";
}

/// Report all paths fanning out from a net/register/port.
std::vector<Path> AnalyseGraph::
getAllFanOut(VertexDesc startVertex) const {
  DEBUG(std::cout << "Performing DFS from "
                  << graph[startVertex].name << "\n");
  ParentMap parentMap;
  boost::depth_first_search(graph,
      boost::visitor(DfsVisitor(parentMap, false))
        .root_vertex(startVertex));
  // Check for a path between startPoint and each register.
  std::vector<Path> paths;
  BGL_FORALL_VERTICES(v, graph, Graph) {
    if (isEndPoint(graph[v].type, graph[v].dir, graph[v].isTop)) {
      auto path = determinePath(parentMap,
                                Path(),
                                startVertex,
                                static_cast<VertexDesc>(graph[v].id));
      std::reverse(std::begin(path), std::end(path));
      paths.push_back(path);
    }
  }
  return paths;
}

std::vector<Path> AnalyseGraph::
getAllFanOut(const std::string &startName) const {
  auto startVertex = getStartVertex(startName);
  return getAllFanOut(startVertex);
}

/// Report all paths fanning into a net/register/port.
std::vector<Path> AnalyseGraph::
getAllFanIn(VertexDesc endVertex) const {
  auto reverseGraph = boost::make_reverse_graph(graph);
  DEBUG(std::cout << "Performing DFS in reverse graph from "
                  << graph[endVertex].name << "\n");
  ParentMap parentMap;
  boost::depth_first_search(reverseGraph,
      boost::visitor(DfsVisitor(parentMap, false))
        .root_vertex(endVertex));
  // Check for a path between endPoint and each register.
  std::vector<Path> paths;
  BGL_FORALL_VERTICES(v, graph, Graph) {
    if (isStartPoint(graph[v].type, graph[v].dir, graph[v].isTop)) {
      auto path = determinePath(parentMap,
                                Path(),
                                endVertex,
                                static_cast<VertexDesc>(graph[v].id));
      paths.push_back(path);
    }
  }
  return paths;
}

std::vector<Path> AnalyseGraph::
getAllFanIn(const std::string &endName) const {
  auto endVertex = getStartVertex(endName);
  return getAllFanIn(endVertex);
}

/// Report a single path between a set of named points.
Path AnalyseGraph::
getAnyPointToPoint() const {
  std::vector<VertexDesc> path;
  // Construct the path between each adjacent waypoints.
  for (std::size_t i = 0; i < waypoints.size()-1; ++i) {
    auto startVertex = waypoints[i];
    auto endVertex = waypoints[i+1];
    DEBUG(std::cout << "Performing DFS from "
                    << graph[startVertex].name << "\n");
    ParentMap parentMap;
    boost::depth_first_search(graph,
        boost::visitor(DfsVisitor(parentMap, false))
          .root_vertex(startVertex));
    DEBUG(std::cout << "Determining a path to "
                    << graph[endVertex].name << "\n");
    auto subPath = determinePath(parentMap,
                                 Path(),
                                 startVertex,
                                 endVertex);
    if (subPath.empty()) {
        throw Exception(std::string("no path from ")
                            +graph[startVertex].name+" to "
                            +graph[endVertex].name);
    }
    std::reverse(std::begin(subPath), std::end(subPath));
    path.insert(std::end(path), std::begin(subPath), std::end(subPath)-1);
  }
  path.push_back(waypoints.back());
  return path;
}

/// Report all paths between start and end points.
std::vector<Path> AnalyseGraph::
getAllPointToPoint() const {
  if (waypoints.size() > 2)
    throw Exception("through points not supported for all paths");
  DEBUG(std::cout << "Performing DFS\n");
  ParentMap parentMap;
  boost::depth_first_search(graph,
      boost::visitor(DfsVisitor(parentMap, true))
        .root_vertex(waypoints[0]));
  DEBUG(std::cout << "Determining all paths\n");
  std::vector<Path> paths;
  determineAllPaths(parentMap,
                    paths,
                    Path(),
                    waypoints[0],
                    waypoints[1]);
  for (auto &path : paths) {
    std::reverse(std::begin(path), std::end(path));
  }
  return paths;
}

std::vector<std::pair<VertexDesc, size_t>> AnalyseGraph::
getAllFanOutDegrees() const {
  DEBUG(std::cout << "Reporting fan outs of all non-logic vertices\n");
  using Pair = std::pair<VertexDesc, size_t>;
  std::vector<Pair> fanOuts;
  BGL_FORALL_VERTICES(v, graph, Graph) {
    if (!isLogic(graph[v].type)) {
       auto fanOut = boost::out_degree(v, graph);
       fanOuts.push_back(std::make_pair(v, fanOut));
    }
  }
  std::sort(std::begin(fanOuts), std::end(fanOuts),
            [](const Pair &a, const Pair &b) -> bool {
                return b.second < a.second; });
  return fanOuts;
}

void AnalyseGraph::
printFanOuts(const std::vector<std::pair<VertexDesc, size_t>> &fanOuts,
             size_t min) const {
  for (auto pair : fanOuts) {
    auto v = pair.first;
    auto degree = pair.second;
    if (degree >= min) {
      std::cout << degree
                << " " << getVertexTypeStr(graph[v].type)
                << " " << graph[v].name << "\n";
    }
  }
}