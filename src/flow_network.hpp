// flow_network.hpp — maximum-flow / minimum-cut algorithms implemented from
// scratch over a shared residual-graph representation.
//
// Three solvers are provided:
//   * Ford-Fulkerson  (DFS augmenting paths)
//   * Dinic's         (BFS level graph + blocking flow via DFS)
//   * Min-Cut         (run a max-flow, then read the cut off the residual graph
//                      using the max-flow / min-cut theorem)
//
// The graph is stored with the classic paired-edge trick: every edge e has a
// companion reverse edge e^1, so residual updates are O(1).
#ifndef UFE_FLOW_NETWORK_HPP
#define UFE_FLOW_NETWORK_HPP

#include <algorithm>
#include <limits>
#include <queue>
#include <vector>

namespace ufe {

using Cap = long long;
constexpr Cap kInf = std::numeric_limits<Cap>::max() / 4;

struct Edge {
  int from;
  int to;
  Cap cap;   // residual capacity (mutated during solving)
  Cap flow;  // flow currently pushed along this edge
};

// Result of a max-flow / min-cut computation.
struct FlowResult {
  Cap max_flow = 0;
  std::vector<bool> source_side;  // node on the source side of the min cut?
};

class FlowNetwork {
 public:
  explicit FlowNetwork(int n) : n_(n), g_(n) {}

  int num_nodes() const { return n_; }

  // Adds an edge u->v with the given capacity. When `directed` is false the
  // reverse direction also carries `cap` (an undirected pipe), otherwise the
  // reverse edge starts at 0 capacity (pure residual edge).
  //
  // Returns the index of the forward edge in edges_ so callers can later read
  // the realized flow for reporting.
  int add_edge(int u, int v, Cap cap, bool directed = true) {
    int forward = (int)edges_.size();
    edges_.push_back({u, v, cap, 0});
    edges_.push_back({v, u, directed ? 0 : cap, 0});
    g_[u].push_back(forward);
    g_[v].push_back(forward + 1);
    return forward;
  }

  const std::vector<Edge>& edges() const { return edges_; }

  // ---- Ford-Fulkerson (DFS augmenting paths) -----------------------------
  FlowResult ford_fulkerson(int s, int t) {
    reset_flow();
    Cap total = 0;
    std::vector<int> visited(n_, 0);
    int stamp = 0;
    while (true) {
      ++stamp;
      Cap pushed = ff_dfs(s, t, kInf, visited, stamp);
      if (pushed == 0) break;
      total += pushed;
    }
    return finalize(s, total);
  }

  // ---- Dinic's algorithm -------------------------------------------------
  FlowResult dinics(int s, int t) {
    reset_flow();
    Cap total = 0;
    level_.assign(n_, -1);
    iter_.assign(n_, 0);
    while (bfs_levels(s, t)) {
      std::fill(iter_.begin(), iter_.end(), 0);
      Cap pushed;
      while ((pushed = dinic_dfs(s, t, kInf)) > 0) total += pushed;
    }
    return finalize(s, total);
  }

  // Min-cut is just a max-flow followed by reading the residual reachability;
  // Dinic's is used as the underlying solver because it is the fastest here.
  FlowResult min_cut(int s, int t) { return dinics(s, t); }

  // After a solve, returns the edges crossing the min cut (source side ->
  // sink side) using the supplied side assignment.
  std::vector<int> cut_edges(const std::vector<bool>& source_side) const {
    std::vector<int> cut;
    for (int e = 0; e < (int)edges_.size(); e += 2) {  // forward edges only
      const Edge& fe = edges_[e];
      if (source_side[fe.from] && !source_side[fe.to])
        cut.push_back(e);
    }
    return cut;
  }

 private:
  void reset_flow() {
    for (auto& e : edges_) {
      // Restore residual capacities: total capacity of an arc is cap+flow.
      e.cap += e.flow;
      e.flow = 0;
    }
  }

  // Compute the source side of the min cut from residual reachability and
  // package up the result.
  FlowResult finalize(int s, Cap total) {
    FlowResult r;
    r.max_flow = total;
    r.source_side.assign(n_, false);
    std::queue<int> q;
    q.push(s);
    r.source_side[s] = true;
    while (!q.empty()) {
      int u = q.front(); q.pop();
      for (int id : g_[u]) {
        const Edge& e = edges_[id];
        if (e.cap > 0 && !r.source_side[e.to]) {
          r.source_side[e.to] = true;
          q.push(e.to);
        }
      }
    }
    return r;
  }

  Cap ff_dfs(int u, int t, Cap f, std::vector<int>& visited, int stamp) {
    if (u == t) return f;
    visited[u] = stamp;
    for (int id : g_[u]) {
      Edge& e = edges_[id];
      if (e.cap > 0 && visited[e.to] != stamp) {
        Cap d = ff_dfs(e.to, t, std::min(f, e.cap), visited, stamp);
        if (d > 0) {
          e.cap -= d;
          e.flow += d;
          edges_[id ^ 1].cap += d;
          edges_[id ^ 1].flow -= d;
          return d;
        }
      }
    }
    return 0;
  }

  bool bfs_levels(int s, int t) {
    std::fill(level_.begin(), level_.end(), -1);
    std::queue<int> q;
    level_[s] = 0;
    q.push(s);
    while (!q.empty()) {
      int u = q.front(); q.pop();
      for (int id : g_[u]) {
        const Edge& e = edges_[id];
        if (e.cap > 0 && level_[e.to] < 0) {
          level_[e.to] = level_[u] + 1;
          q.push(e.to);
        }
      }
    }
    return level_[t] >= 0;
  }

  Cap dinic_dfs(int u, int t, Cap f) {
    if (u == t) return f;
    for (int& cid = iter_[u]; cid < (int)g_[u].size(); ++cid) {
      int id = g_[u][cid];
      Edge& e = edges_[id];
      if (e.cap > 0 && level_[e.to] == level_[u] + 1) {
        Cap d = dinic_dfs(e.to, t, std::min(f, e.cap));
        if (d > 0) {
          e.cap -= d;
          e.flow += d;
          edges_[id ^ 1].cap += d;
          edges_[id ^ 1].flow -= d;
          return d;
        }
      }
    }
    return 0;
  }

  int n_;
  std::vector<Edge> edges_;
  std::vector<std::vector<int>> g_;
  std::vector<int> level_;
  std::vector<int> iter_;
};

}  // namespace ufe

#endif  // UFE_FLOW_NETWORK_HPP
