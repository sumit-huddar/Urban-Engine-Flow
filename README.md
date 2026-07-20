# UrbanFlowEngine

Ask plain-English questions about a real city road network and get plain-English
answers, backed by classic graph algorithms running in C++.

> _"What's the fastest way from Hall 6 to the MT canteen?"_
> _"How much traffic can the roads between Hall 1 and the airstrip handle?"_
> _"Where's the bottleneck between Hall 3 and Hall 12?"_

Under the hood, a Gemini LLM picks the right algorithm and extracts the
locations, a downloaded [OpenStreetMap](https://www.openstreetmap.org/) network
becomes the graph, a small C++ engine runs the computation, and Gemini explains
the raw result back in everyday language.

## How it works

```
                        agent.py  (LLM tool-calling loop)
                            │
   plain-English question ──┤
                            │  1. Gemini picks a tool + extracts source/target
                            │  2. resolve landmark name ──► nearest OSM node
                            ▼
                     osm_to_engine.py   (OSMnx graph ──► engine JSON)
                            │
                            ▼
                       engine.py        (subprocess bridge, JSON over stdin/stdout)
                            │
                            ▼
                      ./engine          (compiled C++ solver: engine.cpp)
                            │
                            ▼
                            │  3. raw JSON result
                            │  4. Gemini explains it in plain English
                            ▼
                        answer to user
```

### The three questions it can answer

| User intent | LLM tool | Algorithm | Engine returns |
|---|---|---|---|
| Fastest/shortest route | `find_shortest_route` | Dijkstra | distance + node path |
| How much traffic can flow | `find_max_traffic_flow` | Dinic's max-flow | max flow (vehicles/hour) |
| Where's the bottleneck | `find_traffic_bottleneck` | Min-cut | cut value, cut edges, both sides |

## Components

| File | Role |
|---|---|
| `engine.cpp` | The C++ engine: Dijkstra, Dinic's max-flow, and min-cut over a JSON graph read from stdin. |
| `engine` | Compiled binary (git-ignored — you build it). |
| `json.hpp` | [nlohmann/json](https://github.com/nlohmann/json) single-header library used by the engine. |
| `engine.py` | Python ↔ C++ bridge. Runs the binary as a subprocess, passing JSON over stdin/stdout. |
| `osm_to_engine.py` | Converts an OSMnx road network (networkx graph) into the JSON shape the engine expects, assigning synthetic per-lane road capacities. |
| `agent.py` | The LLM tool-calling loop: question → tool selection → engine → plain-English explanation. |
| `test.py` | Quick end-to-end check against a real downloaded network. |
| `requirements.txt` | Python dependencies (the C++ engine has none). |

## Setup

### 1. Build the C++ engine

```bash
g++ -O2 -std=c++17 engine.cpp -o engine
```

### 2. Install Python dependencies

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Dependencies: `osmnx` (download/model OSM networks), `scikit-learn` (required by
`osmnx.distance.nearest_nodes`), and `google-genai` (Gemini tool-calling client).

### 3. Set your Gemini API key

```bash
export GOOGLE_API_KEY="your-key-here"
```

## Usage

### Interactive agent

```bash
python agent.py
```

```
Urban Flow AI Engine -- loading road network...
Loaded 412 nodes, 1043 edges.
Known landmarks: hall 6, hall 12, mt canteen, hall 1, ...
Type a question (or 'quit' to exit).

> what's the fastest way from hall 6 to mt canteen?
The quickest route is about 0.9 km along local roads...
```

On startup the agent downloads a road network around `CENTER_POINT` and only
recognizes the named landmarks in the `LANDMARKS` dictionary. Both are configured
near the top of `agent.py` — edit them for your own area.

### The C++ engine on its own

The engine reads one JSON query from stdin and prints one JSON result to stdout:

```bash
echo '{
  "algorithm": "dijkstra",
  "num_nodes": 5,
  "source": 0,
  "target": 4,
  "edges": [
    {"from": 0, "to": 1, "weight": 2, "undirected": true},
    {"from": 1, "to": 2, "weight": 2, "undirected": true},
    {"from": 0, "to": 3, "weight": 5, "undirected": true},
    {"from": 3, "to": 4, "weight": 1, "undirected": true},
    {"from": 2, "to": 4, "weight": 3, "undirected": true}
  ]
}' | ./engine
```

```json
{"algorithm":"dijkstra","distance":6.0,"found":true,"path":[0,3,4]}
```

**Query fields**

- `algorithm` — `"dijkstra"`, `"dinic"`, or `"min_cut"`
- `num_nodes` — nodes are dense 0-indexed integers (`0 .. num_nodes-1`)
- `source`, `target` — node indices
- `edges` — each has `from`, `to`, and either `weight` (Dijkstra) or `capacity`
  (Dinic's / min-cut). Set `"undirected": true` to add both directions.

### Testing

```bash
python engine.py   # runs the built-in bridge tests (Dijkstra, Dinic's, min-cut, error handling)
python test.py     # full end-to-end run against a freshly downloaded network (needs internet)
```

## Notes

- **Internet access** is required to download OSM data (`agent.py`, `test.py`).
  The C++ engine and `engine.py` bridge tests run fully offline.
- Road capacities are synthetic per-lane approximations (loosely based on
  Highway Capacity Manual throughput by road class), not certified figures — see
  `CAPACITY_PER_LANE` in `osm_to_engine.py`.
- OSMnx already represents two-way streets as two directed edges, so
  `osm_to_engine.py` deliberately does **not** mark edges `undirected`.
</content>
</invoke>
