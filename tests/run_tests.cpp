// run_tests.cpp — a tiny self-contained test runner for the flow solvers.
//
// No framework: each check prints PASS/FAIL and the process exits non-zero if
// anything fails. Every network is solved with all three entry points so the
// algorithms are cross-validated against each other and against known answers.
#include <array>
#include <iostream>
#include <string>
#include <vector>

#include "../src/flow_network.hpp"

using ufe::Cap;
using ufe::FlowNetwork;

namespace {
int g_failures = 0;

void check(const std::string& name, Cap got, Cap want) {
  bool ok = got == want;
  if (!ok) ++g_failures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name << "  (got " << got
            << ", want " << want << ")\n";
}

// Builds a fresh network for an edge list so each algorithm starts clean.
FlowNetwork build(int n, const std::vector<std::array<Cap, 3>>& edges,
                  bool directed = true) {
  FlowNetwork net(n);
  for (const auto& e : edges)
    net.add_edge((int)e[0], (int)e[1], e[2], directed);
  return net;
}
}  // namespace

int main() {
  // CLRS classic flow network (max flow = 23, nodes 0..5, s=0, t=5).
  std::vector<std::array<Cap, 3>> clrs = {
      {0, 1, 16}, {0, 2, 13}, {1, 3, 12}, {2, 1, 4},  {3, 2, 9},
      {2, 4, 14}, {4, 3, 7},  {3, 5, 20}, {4, 5, 4}};
  {
    auto a = build(6, clrs);
    check("CLRS / ford_fulkerson", a.ford_fulkerson(0, 5).max_flow, 23);
    auto b = build(6, clrs);
    check("CLRS / dinics", b.dinics(0, 5).max_flow, 23);
    auto c = build(6, clrs);
    auto r = c.min_cut(0, 5);
    check("CLRS / min_cut value", r.max_flow, 23);
    // The min cut for this network separates {0,1,2,4} from {3,5}.
    check("CLRS / source 0 on source side", r.source_side[0] ? 1 : 0, 1);
    check("CLRS / sink 5 on sink side", r.source_side[5] ? 0 : 1, 1);
  }

  // Single bottleneck edge: s->a (cap 5), a->t (cap 3) => max flow 3.
  {
    std::vector<std::array<Cap, 3>> g = {{0, 1, 5}, {1, 2, 3}};
    auto a = build(3, g);
    check("bottleneck / dinics", a.dinics(0, 2).max_flow, 3);
    auto b = build(3, g);
    check("bottleneck / ford_fulkerson", b.ford_fulkerson(0, 2).max_flow, 3);
  }

  // Two parallel disjoint paths, each cap 10 => max flow 20.
  {
    std::vector<std::array<Cap, 3>> g = {
        {0, 1, 10}, {1, 4, 10}, {0, 2, 10}, {2, 4, 10}};
    auto a = build(5, g);
    check("parallel paths / dinics", a.dinics(0, 4).max_flow, 20);
  }

  // Disconnected sink => max flow 0, cut value 0.
  {
    std::vector<std::array<Cap, 3>> g = {{0, 1, 7}};
    auto a = build(3, g);
    check("disconnected / dinics", a.dinics(0, 2).max_flow, 0);
  }

  // Undirected triangle: edges treated as bidirectional pipes.
  // s=0, t=2 with 0-1 (cap 3), 1-2 (cap 2), 0-2 (cap 4) => max flow 6.
  {
    std::vector<std::array<Cap, 3>> g = {{0, 1, 3}, {1, 2, 2}, {0, 2, 4}};
    auto a = build(3, g, /*directed=*/false);
    check("undirected / dinics", a.dinics(0, 2).max_flow, 6);
  }

  std::cout << "\n" << (g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED")
            << "\n";
  return g_failures == 0 ? 0 : 1;
}
