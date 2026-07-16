#include <bits/stdc++.h>
#include "json.hpp"
using json = nlohmann::json;
using namespace std;
// `rev` is the index, within the OTHER node's adjacency list, of
// this edge's paired reverse edge. This is what lets the flow
// algorithms "undo" flow they've pushed — a core idea in residual
// graphs. It is set up for you in buildGraph() below; you don't
// need to build this wiring yourself, just understand that it
// exists and how to use it (you'll need g.adj[u.to][u.rev] inside
// your flow algorithms).
struct Edge {
    int to;
    double weight;
    long long cap;
    long long flow = 0;
    int rev = -1;
};
struct Graph {
    int n;
    vector<vector<Edge>> adj;
    Graph(int vertices) : n(vertices), adj(vertices) {}
    void addEdge(int from, int to, double w, long long cap) {
        Edge a = {to, w, cap, 0, (int)adj[to].size()};
    Edge b = {from, 0, 0,   0, (int)adj[from].size()};

    adj[from].push_back(a);
    adj[to].push_back(b);
    }
};
Graph buildGraphForDijkstra(const json& input) {
    // Dijkstra only needs weighted adjacency — no capacities and no residual
    // reverse edges. Using addEdge() here would inject spurious zero-weight
    // reverse edges into the search, so we push plain forward edges instead.
    int n = input.at("num_nodes").get<int>();
    Graph g(n);
    for (auto& e : input.at("edges")) {
        int u = e.at("from").get<int>();
        int v = e.at("to").get<int>();
        double w = e.value("weight", 1.0);
        g.adj[u].push_back({v, w, 0, 0, -1});
        if (e.value("undirected", false)) {
            g.adj[v].push_back({u, w, 0, 0, -1});
        }
    }
    return g;
}
Graph buildGraph(const json& input) {
    int n = input.at("num_nodes").get<int>();
    Graph g(n);
    for (auto& e : input.at("edges")) {
        int u = e.at("from").get<int>();
        int v = e.at("to").get<int>();
        double w = e.value("weight", 1.0);
        long long cap = e.value("capacity", 0LL);
        g.addEdge(u, v, w, cap);
        if (e.value("undirected", false)) {
            g.addEdge(v, u, w, cap);
        }
    }
    return g;
}
json runDijkstra(const Graph& g, int source, int target) {
    json result;
    const double INF = numeric_limits<double>::infinity();
    vector<double> dist(g.n, INF);
    vector<int> prev(g.n, -1);
    dist[source] = 0.0;

    // Lazy-deletion Dijkstra with a min-heap keyed on tentative distance.
    priority_queue<pair<double, int>, vector<pair<double, int>>,
                   greater<pair<double, int>>> pq;
    pq.push({0.0, source});
    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[u]) continue;  // stale queue entry
        for (const Edge& e : g.adj[u]) {
            if (dist[u] + e.weight < dist[e.to]) {
                dist[e.to] = dist[u] + e.weight;
                prev[e.to] = u;
                pq.push({dist[e.to], e.to});
            }
        }
    }

    bool found = dist[target] != INF;
    result["found"] = found;
    if (found) {
        result["distance"] = dist[target];
        // Walk predecessors from target back to source to recover the path.
        vector<int> path;
        for (int at = target; at != -1; at = prev[at]) path.push_back(at);
        reverse(path.begin(), path.end());
        result["path"] = path;
    }
    return result;
}
json runDinic(Graph& g, int source, int target) {
    json result;
    long long flow = 0;

    vector<int> level(g.n), it(g.n);

    // BFS builds the level graph over edges with residual capacity (cap - flow).
    auto bfs = [&]() -> bool {
        fill(level.begin(), level.end(), -1);
        queue<int> q;
        level[source] = 0;
        q.push(source);
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (const Edge& e : g.adj[u]) {
                if (level[e.to] < 0 && e.cap - e.flow > 0) {
                    level[e.to] = level[u] + 1;
                    q.push(e.to);
                }
            }
        }
        return level[target] >= 0;
    };

    // DFS sends blocking flow along level-graph edges. `it` remembers how far
    // we've advanced in each adjacency list so exhausted edges aren't revisited.
    function<long long(int, long long)> dfs = [&](int u, long long pushed) -> long long {
        if (u == target) return pushed;
        for (int& i = it[u]; i < (int)g.adj[u].size(); ++i) {
            Edge& e = g.adj[u][i];
            if (level[e.to] == level[u] + 1 && e.cap - e.flow > 0) {
                long long d = dfs(e.to, min(pushed, e.cap - e.flow));
                if (d > 0) {
                    e.flow += d;
                    g.adj[e.to][e.rev].flow -= d;  // give the reverse edge capacity to undo
                    return d;
                }
            }
        }
        return 0;
    };

    while (bfs()) {
        fill(it.begin(), it.end(), 0);
        while (long long pushed = dfs(source, LLONG_MAX)) flow += pushed;
    }

    result["found"] = true;
    result["max_flow"] = flow;
    return result;
}
json runMinCut(Graph& g, int source, int target) {
    json result;
    result["found"] = true;

    // Min-cut capacity == max flow (max-flow / min-cut theorem). Saturate the
    // network first, then read the cut off the residual graph.
    json flowRes = runDinic(g, source, target);
    long long cutValue = flowRes.at("max_flow").get<long long>();

    // Nodes reachable from the source in the residual graph = the source side.
    vector<char> reachable(g.n, 0);
    queue<int> q;
    q.push(source);
    reachable[source] = 1;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        for (const Edge& e : g.adj[u]) {
            if (!reachable[e.to] && e.cap - e.flow > 0) {
                reachable[e.to] = 1;
                q.push(e.to);
            }
        }
    }

    json sourceSide = json::array();
    json sinkSide = json::array();
    for (int u = 0; u < g.n; ++u) {
        if (reachable[u]) sourceSide.push_back(u);
        else sinkSide.push_back(u);
    }

    // Cut edges: original (positive-capacity) edges from source side to sink
    // side; their capacities sum to the max flow.
    json cutEdges = json::array();
    for (int u = 0; u < g.n; ++u) {
        if (!reachable[u]) continue;
        for (const Edge& e : g.adj[u]) {
            if (!reachable[e.to] && e.cap > 0) {
                cutEdges.push_back({{"from", u}, {"to", e.to}, {"capacity", e.cap}});
            }
        }
    }

    result["min_cut_value"] = cutValue;
    result["source_side"] = sourceSide;
    result["sink_side"] = sinkSide;
    result["cut_edges"] = cutEdges;
    return result;
}

// ==================================================================
// DISPATCH — given, you shouldn't need to modify this
// ==================================================================
int main() {
    ostringstream ss;
    ss << cin.rdbuf();
    string input_str = ss.str();

    json input;
    try {
        input = json::parse(input_str);
    } catch (const exception& ex) {
        json err;
        err["error"] = string("invalid JSON input: ") + ex.what();
        std::cout << err.dump() << std::endl;
        return 1;
    }

    string algorithm = input.value("algorithm", "");
    int source = input.value("source", 0);
    int target = input.value("target", 0);

    json result;
    try {
        if (algorithm == "dijkstra") {
            Graph g = buildGraphForDijkstra(input);
            result = runDijkstra(g, source, target);
        } else if (algorithm == "dinic") {
            Graph g = buildGraph(input);
            result = runDinic(g, source, target);
        } else if (algorithm == "min_cut") {
            Graph g = buildGraph(input);
            result = runMinCut(g, source, target);
        } else {
            result["error"] = "unknown algorithm: " + algorithm;
            result["algorithm"] = algorithm;
            std::cout << result.dump() << std::endl;
            return 1;
        }
    } catch (const exception& ex) {
        result = json{};
        result["error"] = string("runtime error: ") + ex.what();
        std::cout << result.dump() << std::endl;
        return 1;
    }

    result["algorithm"] = algorithm;
    std::cout << result.dump() << std::endl;
    return 0;
}
