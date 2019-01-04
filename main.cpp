#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include "netlist_paths/AnalyseGraph.hpp"
#include "netlist_paths/CompileGraph.hpp"
#include "netlist_paths/Exception.hpp"
#include "netlist_paths/Options.hpp"
#include "netlist_paths/utilities.hpp"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

netlist_paths::Options options;

int main(int argc, char **argv) {
  try {
    // Command line options.
    po::options_description hiddenOptions("Positional options");
    po::options_description genericOptions("General options");
    po::options_description allOptions("All options");
    po::positional_options_description p;
    po::variables_map vm;
    size_t maxFanOut;
    std::vector<std::string> inputFiles;
    std::string outputFilename;
    std::string startName;
    std::string endName;
    std::vector<std::string> throughNames;
    // Specify command line options.
    hiddenOptions.add_options()
      ("input-file",
       po::value<std::vector<std::string>>(&inputFiles)->required());
    p.add("input-file", -1);
    genericOptions.add_options()
      ("help,h",        "Display help")
      ("from",          po::value<std::string>(&startName)
                          ->value_name("name"),
                        "Start point")
      ("to",            po::value<std::string>(&endName)
                          ->value_name("name"),
                        "End point")
      ("through",       po::value<std::vector<std::string>>(&throughNames)
                          ->composing()
                          ->value_name("name"),
                        "Through point")
      ("allpaths",      "Find all paths between two points (exponential time)")
      ("allfanout",     po::value<size_t>(&maxFanOut)
                          ->value_name("max")
                          ->implicit_value(16),
                        "List the fan out of each register")
      ("reportlogic",   "Display logic in path report")
      ("filenames",     "Display full filenames in path report")
      ("compile",       "Compile a netlist graph from Verilog source")
      ("include,I",     po::value<std::vector<std::string>>()
                          ->composing()
                          ->value_name("path"),
                        "include path (only with --compile)")
      ("define,D",      po::value<std::vector<std::string>>()
                          ->composing()
                          ->value_name("path"),
                        "define a preprocessor macro (only with --compile)")
      ("dotfile",       "Dump dotfile of netlist graph")
      ("dumpnames",     "Dump list of names in netlist")
      ("outfile,o",     po::value<std::string>(&outputFilename)
                          ->default_value(netlist_paths::DEFAULT_OUTPUT_FILENAME)
                          ->value_name("filename"),
                        "output file")
      ("boostparser",   "Use the boost GraphViz parser")
      ("debug",         "Print debugging information");
    allOptions.add(genericOptions).add(hiddenOptions);
    // Parse command line arguments.
    po::store(po::command_line_parser(argc, argv).
                  options(allOptions).positional(p).run(), vm);
    options.debugMode     = vm.count("debug") > 0;
    options.displayHelp   = vm.count("help");
    options.dumpDotfile   = vm.count("dotfile");
    options.dumpNames     = vm.count("dumpnames");
    options.allFanOut     = vm.count("allfanout");
    options.allPaths      = vm.count("allpaths");
    options.reportLogic   = vm.count("reportlogic");
    options.fullFileNames = vm.count("filenames");
    options.compile       = vm.count("compile");
    options.boostParser   = vm.count("boostparser");
    if (options.displayHelp) {
      std::cout << "OVERVIEW: Query paths in a Verilog netlist\n\n";
      std::cout << "USAGE: " << argv[0] << " [options] infile\n\n";
      std::cout << genericOptions << "\n";
      return 1;
    }
    notify(vm);

    // Call Verilator to produce graph file.
    if (options.compile) {
      if (outputFilename == netlist_paths::DEFAULT_OUTPUT_FILENAME)
         outputFilename += ".graph";
      auto includes = vm.count("include")
                        ? vm["include"].as<std::vector<std::string>>()
                        : std::vector<std::string>{};
      auto defines = vm.count("define")
                        ? vm["define"].as<std::vector<std::string>>()
                        : std::vector<std::string>{};
      netlist_paths::CompileGraph compileGraph;
      return compileGraph.run(includes,
                              defines,
                              inputFiles,
                              outputFilename);
    }

    netlist_paths::AnalyseGraph analyseGraph;

    // Parse the input file.
    if (inputFiles.size() > 1)
      throw netlist_paths::Exception("multiple graph files specified");
    analyseGraph.parseFile(inputFiles.front());
    analyseGraph.checkGraph();

    // Dump dot file.
    if (options.dumpDotfile) {
      if (outputFilename == netlist_paths::DEFAULT_OUTPUT_FILENAME)
         outputFilename += ".dot";
      analyseGraph.dumpDotFile(outputFilename);
      return 0;
    }

    // Dump netlist names.
    if (options.dumpNames) {
      analyseGraph.dumpVertexNames();
      return 0;
    }

    // Report the fan-out degree for each register.
    if (options.allFanOut) {
      auto fanOuts = analyseGraph.getAllFanOutDegrees();
      analyseGraph.printFanOuts(fanOuts, maxFanOut);
      return 0;
    }

    // A start or an endpoint must be specified.
    if (startName.empty() && endName.empty()) {
      throw netlist_paths::Exception("no start and/or end point specified");
    }

    // Report paths fanning out from startName.
    if (!startName.empty() && endName.empty()) {
      if (!throughNames.empty())
        throw netlist_paths::Exception("through points not supported for start only");
      auto paths = analyseGraph.getAllFanOut(startName);
      analyseGraph.printPathReport(paths);
      return 0;
    }

    // Report paths fanning in to endName.
    if (startName.empty() && !endName.empty()) {
      if (!throughNames.empty())
        throw netlist_paths::Exception("through points not supported for end only");
      auto paths = analyseGraph.getAllFanIn(endName);
      analyseGraph.printPathReport(paths);
      return 0;
    }

    // Compile path waypoints.
    analyseGraph.addStartpoint(startName);
    for (auto &throughName : throughNames) {
      analyseGraph.addWaypoint(throughName);
    }
    analyseGraph.addEndpoint(endName);

    // Report all paths between two points.
    if (options.allPaths) {
      auto paths = analyseGraph.getAllPointToPoint();
      analyseGraph.printPathReport(paths);
      return 0;
    }

    // Report a path between two points.
    auto path = analyseGraph.getAnyPointToPoint();
    analyseGraph.printPathReport(path);
    return 0;
  } catch (std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
