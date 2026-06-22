// main.cpp — the Urban Flow Engine flow binary.
//
// A single callable binary that reads a JSON description of a capacitated graph
// plus a source/sink, runs one of the from-scratch max-flow / min-cut solvers,
// and emits a JSON result. Designed to be driven by another process (e.g. the
// LLM tool-calling agent) over stdin/stdout or files.
//
// Usage:
//   flow [input.json] [-o output.json] [--algorithm dinics|ford_fulkerson|min_cut]
//
//   * With no input path, JSON is read from stdin.
//   * With no -o, JSON is written to stdout.
//   * --algorithm overrides the "algorithm" field in the input.
//
// Input schema:
//   {
//     "algorithm": "dinics",      // optional; default "dinics"
//     "directed": true,            // optional; default true
//     "source": "A",               // node label (number or string)
//     "sink": "F",
//     "nodes": ["A","B", ...],     // optional; fixes node ordering
//     "edges": [ {"u": "A", "v": "B", "capacity": 16}, ... ]
//   }
//
// Output schema:
//   {
//     "algorithm": "...",
//     "source": ..., "sink": ...,
//     "max_flow": 23,
//     "min_cut": { "value": 23, "source_side": [...], "sink_side": [...],
//                  "edges": [ {"u":...,"v":...,"capacity":...} ] },
//     "flow_edges": [ {"u":...,"v":...,"capacity":...,"flow":...} ]
//   }
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "flow_network.hpp"
#include "json.hpp"

namespace {

using ufe::Cap;
using ufe::FlowNetwork;
using ufe::FlowResult;

// A JSON scalar used as a node label may be a number or a string. We keep a
// canonical string form for lookup, plus the original Value for output so that
// numeric OSM ids round-trip as numbers rather than strings.
struct Label {
  std::string key;            // canonical key for de-duplication
  ufe::json::Value original;  // value to echo back in output
};

Label make_label(const ufe::json::Value& v) {
  if (v.type() == ufe::json::Type::String)
    return {"s:" + v.as_string(), v};
  if (v.is_number()) {
    std::ostringstream os;
    os << v.as_int();
    return {"n:" + os.str(), v};
  }
  throw std::runtime_error("node labels must be a string or integer");
}

std::string read_all(std::istream& in) {
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Dispatches by name. Defaults to Dinic's for unknown values.
FlowResult run_solver(const std::string& algo, FlowNetwork& net, int s, int t) {
  if (algo == "ford_fulkerson") return net.ford_fulkerson(s, t);
  if (algo == "min_cut") return net.min_cut(s, t);
  return net.dinics(s, t);
}

}  // namespace

int main(int argc, char** argv) {
  std::string input_path, output_path, algo_override;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-o" && i + 1 < argc) {
      output_path = argv[++i];
    } else if (a == "--algorithm" && i + 1 < argc) {
      algo_override = argv[++i];
    } else if (a == "-h" || a == "--help") {
      std::cerr << "usage: flow [input.json] [-o output.json] "
                   "[--algorithm dinics|ford_fulkerson|min_cut]\n";
      return 0;
    } else if (!a.empty() && a[0] != '-') {
      input_path = a;
    }
  }

  try {
    // --- Read input -------------------------------------------------------
    std::string text;
    if (input_path.empty()) {
      text = read_all(std::cin);
    } else {
      std::ifstream f(input_path);
      if (!f) throw std::runtime_error("cannot open input file: " + input_path);
      text = read_all(f);
    }

    ufe::json::Value doc = ufe::json::parse(text);
    if (!doc.is_object())
      throw std::runtime_error("top-level JSON must be an object");

    std::string algo = algo_override;
    if (algo.empty())
      algo = doc.has("algorithm") ? doc.at("algorithm").as_string() : "dinics";
    bool directed = doc.has("directed") ? doc.at("directed").as_bool() : true;

    if (!doc.has("source") || !doc.has("sink"))
      throw std::runtime_error("input requires 'source' and 'sink'");
    if (!doc.has("edges"))
      throw std::runtime_error("input requires an 'edges' array");

    // --- Map node labels to dense indices --------------------------------
    std::unordered_map<std::string, int> index;
    std::vector<Label> labels;
    auto intern = [&](const ufe::json::Value& v) -> int {
      Label lab = make_label(v);
      auto it = index.find(lab.key);
      if (it != index.end()) return it->second;
      int id = (int)labels.size();
      index[lab.key] = id;
      labels.push_back(lab);
      return id;
    };

    // Honor an explicit node ordering if provided.
    if (doc.has("nodes")) {
      for (const auto& n : doc.at("nodes").as_array()) intern(n);
    }

    int s = intern(doc.at("source"));
    int t = intern(doc.at("sink"));

    // First pass: intern all edge endpoints so the graph size is known.
    const auto& edges_json = doc.at("edges").as_array();
    struct InEdge { int u, v; Cap cap; };
    std::vector<InEdge> in_edges;
    in_edges.reserve(edges_json.size());
    for (const auto& e : edges_json) {
      if (!e.has("u") || !e.has("v"))
        throw std::runtime_error("each edge requires 'u' and 'v'");
      int u = intern(e.at("u"));
      int v = intern(e.at("v"));
      Cap cap = e.has("capacity") ? (Cap)e.at("capacity").as_int() : 1;
      if (cap < 0) throw std::runtime_error("edge capacities must be >= 0");
      in_edges.push_back({u, v, cap});
    }

    // --- Build the flow network ------------------------------------------
    FlowNetwork net((int)labels.size());
    std::vector<int> edge_ids;
    edge_ids.reserve(in_edges.size());
    for (const auto& e : in_edges)
      edge_ids.push_back(net.add_edge(e.u, e.v, e.cap, directed));

    if (s == t) throw std::runtime_error("source and sink must differ");

    // --- Solve -----------------------------------------------------------
    FlowResult res = run_solver(algo, net, s, t);

    // --- Build output ----------------------------------------------------
    using ufe::json::Value;
    Value out = Value::object();
    out["algorithm"] = Value(algo);
    out["directed"] = Value(directed);
    out["source"] = labels[s].original;
    out["sink"] = labels[t].original;
    out["max_flow"] = Value((long long)res.max_flow);

    // Per-edge realized flow (forward edges only, in input order).
    Value flow_edges = Value::array();
    for (size_t i = 0; i < edge_ids.size(); ++i) {
      const ufe::Edge& fe = net.edges()[edge_ids[i]];
      Value fe_json = Value::object();
      fe_json["u"] = labels[fe.from].original;
      fe_json["v"] = labels[fe.to].original;
      fe_json["capacity"] = Value((long long)(fe.cap + fe.flow));
      fe_json["flow"] = Value((long long)fe.flow);
      flow_edges.push_back(fe_json);
    }
    out["flow_edges"] = flow_edges;

    // Min cut derived from the residual graph.
    Value cut = Value::object();
    cut["value"] = Value((long long)res.max_flow);
    Value src_side = Value::array(), sink_side = Value::array();
    for (int i = 0; i < net.num_nodes(); ++i) {
      if (res.source_side[i]) src_side.push_back(labels[i].original);
      else sink_side.push_back(labels[i].original);
    }
    cut["source_side"] = src_side;
    cut["sink_side"] = sink_side;
    Value cut_edges = Value::array();
    for (int e : net.cut_edges(res.source_side)) {
      const ufe::Edge& fe = net.edges()[e];
      Value ce = Value::object();
      ce["u"] = labels[fe.from].original;
      ce["v"] = labels[fe.to].original;
      ce["capacity"] = Value((long long)(fe.cap + fe.flow));
      cut_edges.push_back(ce);
    }
    cut["edges"] = cut_edges;
    out["min_cut"] = cut;

    std::string rendered = out.dump();
    if (output_path.empty()) {
      std::cout << rendered << "\n";
    } else {
      std::ofstream f(output_path);
      if (!f) throw std::runtime_error("cannot open output file: " + output_path);
      f << rendered << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
}
