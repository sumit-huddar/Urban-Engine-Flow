# Urban Flow Engine

A single, callable C++ binary that answers **network-flow queries** over
capacitated graphs. It implements **Ford-Fulkerson**, **Dinic's**, and
**min-cut** from scratch (no third-party libraries) and speaks **JSON** over
stdin/stdout or files, so it can be driven directly by another program — for
example an LLM tool-calling agent or a city-network traffic model.

This repository is the flow-solver core of the Urban Flow Engine project. It is
deliberately self-contained: the graph algorithms, the JSON parser/serializer,
and the CLI all live here with zero external dependencies.

## Build

Requires only a C++17 compiler (`clang` or `gcc`).

```sh
make            # builds bin/flow
make test       # builds and runs the test suite
make run-example
```

## Usage

```sh
flow [input.json] [-o output.json] [--algorithm dinics|ford_fulkerson|min_cut]
```

* With no input path, JSON is read from **stdin**.
* With no `-o`, JSON is written to **stdout**.
* `--algorithm` overrides the `"algorithm"` field in the input.

```sh
# From a file
./bin/flow examples/clrs_network.json

# Piped, choosing the solver on the command line
cat examples/city_grid.json | ./bin/flow --algorithm ford_fulkerson
```

## Input schema

```jsonc
{
  "algorithm": "dinics",      // optional: dinics | ford_fulkerson | min_cut (default dinics)
  "directed": true,            // optional: default true; false = undirected pipes
  "source": "s",               // node label — a string or an integer
  "sink": "t",
  "nodes": ["s", "v1", "t"],   // optional: fixes node ordering in the output
  "edges": [
    { "u": "s", "v": "v1", "capacity": 16 }
  ]
}
```

Node labels may be **strings** or **integers** (integers round-trip as numbers,
so raw OSM node ids work directly). `capacity` defaults to `1` if omitted.

## Output schema

```jsonc
{
  "algorithm": "dinics",
  "directed": true,
  "source": "s",
  "sink": "t",
  "max_flow": 23,
  "flow_edges": [                 // realized flow per input edge
    { "u": "s", "v": "v1", "capacity": 16, "flow": 12 }
  ],
  "min_cut": {                    // derived from the residual graph
    "value": 23,
    "source_side": ["s", "v1", "v2", "v4"],
    "sink_side":   ["v3", "t"],
    "edges": [                    // edges crossing the cut, source -> sink side
      { "u": "v1", "v": "v3", "capacity": 12 }
    ]
  }
}
```

The capacities of the `min_cut.edges` always sum to `max_flow`, as guaranteed by
the max-flow / min-cut theorem.

## Algorithms

| Algorithm        | Augmenting-path strategy            | Complexity                |
|------------------|-------------------------------------|---------------------------|
| Ford-Fulkerson   | DFS augmenting paths                | O(E · max_flow)           |
| Dinic's          | BFS level graph + blocking-flow DFS | O(V² · E)                 |
| Min-cut          | Max-flow, then residual reachability | same as Dinic's          |

All three share one residual-graph representation (paired forward/reverse edges
for O(1) residual updates) in [`src/flow_network.hpp`](src/flow_network.hpp).
The from-scratch JSON layer is in [`src/json.hpp`](src/json.hpp) and the CLI in
[`src/main.cpp`](src/main.cpp).

## Layout

```
src/flow_network.hpp   max-flow / min-cut solvers
src/json.hpp           minimal JSON parser + serializer
src/main.cpp           JSON-driven CLI (the `flow` binary)
tests/run_tests.cpp    cross-validated correctness tests
examples/              sample graph inputs
```

## Tests

```sh
make test
```

The suite checks each solver against the textbook CLRS network (max flow 23),
bottleneck and parallel-path graphs, a disconnected sink, and an undirected
network, cross-validating the three algorithms against one another.
