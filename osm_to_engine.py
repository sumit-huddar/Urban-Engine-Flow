"""
osm_to_engine.py
------------------------------------------------------------------
Converts a real OSMnx road network (a networkx MultiDiGraph) into
the JSON shape engine.cpp expects. This is the missing link between
"download a real city" and "feed it to your compiled C++ binary."

Usage on your own machine (network access required for the download
step -- this won't run in a sandboxed environment without internet
access to OpenStreetMap's servers):

    import osmnx as ox
    from osm_to_engine import osmnx_graph_to_engine_json

    # Small radius around a point keeps you in the 50-150 node range
    # without needing to manually subsample afterward.
    G = ox.graph_from_point((26.4499, 80.3319), dist=800, network_type="drive")
    print(G.number_of_nodes())  # check you're in a reasonable range

    nodes = list(G.nodes())
    query = osmnx_graph_to_engine_json(G, nodes[0], nodes[-1], algorithm="dinic")

    from engine import run_engine
    result = run_engine(query)
    print(result)
------------------------------------------------------------------
"""

# Simplified, synthetic per-lane capacity (vehicles/hour), loosely
# modeled on Highway Capacity Manual style throughput numbers by
# road classification. These are reasonable approximations for this
# project, not HCM-certified figures.
CAPACITY_PER_LANE = {
    "motorway": 2000,
    "trunk": 1800,
    "primary": 1500,
    "secondary": 1000,
    "tertiary": 800,
    "unclassified": 500,
    "residential": 400,
    "living_street": 200,
}
DEFAULT_CAPACITY_PER_LANE = 500
DEFAULT_LANES = 1


def _highway_tag(edge_data: dict) -> str:
    """OSMnx sometimes stores 'highway' as a single string, sometimes
    as a list (when a road's classification changes partway along
    its length, OSM tags it with multiple values). Just take the
    first one -- good enough for synthetic capacity purposes."""
    h = edge_data.get("highway", "unclassified")
    if isinstance(h, list):
        h = h[0]
    return h


def _lane_count(edge_data: dict) -> int:
    lanes = edge_data.get("lanes", DEFAULT_LANES)
    if isinstance(lanes, list):
        lanes = lanes[0]
    try:
        return max(1, int(lanes))
    except (TypeError, ValueError):
        return DEFAULT_LANES


def _capacity_for_edge(edge_data: dict) -> int:
    highway = _highway_tag(edge_data)
    per_lane = CAPACITY_PER_LANE.get(highway, DEFAULT_CAPACITY_PER_LANE)
    return per_lane * _lane_count(edge_data)


def osmnx_graph_to_engine_json(G, source_osm_id, target_osm_id, algorithm="dinic"):
    """
    G: an OSMnx graph (networkx MultiDiGraph), e.g. from
       ox.graph_from_point(...) or ox.graph_from_place(...)
    source_osm_id, target_osm_id: real OSM node IDs (from G.nodes(),
       or via ox.distance.nearest_nodes if you only have lat/lon)
    algorithm: which engine.cpp algorithm this query targets

    Returns a dict ready for json.dumps() / engine.run_engine().
    """
    # OSMnx node IDs are large OSM-assigned integers, not 0..n-1.
    # engine.cpp expects dense 0-indexed node IDs (it sizes its
    # adjacency list off "num_nodes"), so remap here.
    osm_id_to_dense = {osm_id: i for i, osm_id in enumerate(G.nodes())}

    edges = []
    for u, v, data in G.edges(data=True):
        weight = data.get("length", 1.0)  # real road length in meters
        capacity = _capacity_for_edge(data)
        edges.append({
            "from": osm_id_to_dense[u],
            "to": osm_id_to_dense[v],
            "weight": weight,
            "capacity": capacity,
            # Deliberately NOT setting "undirected" here. OSMnx already
            # represents two-way streets as two separate directed edges
            # (u->v AND v->u, each with their own data). Marking these
            # "undirected" too would double them up -- the same class of
            # bug as the old addEdge() double-reverse issue from
            # engine.cpp. One real OSMnx edge = one real JSON edge.
        })

    if source_osm_id not in osm_id_to_dense or target_osm_id not in osm_id_to_dense:
        raise ValueError("source_osm_id / target_osm_id must be node IDs that exist in G")

    return {
        "algorithm": algorithm,
        "num_nodes": len(osm_id_to_dense),
        "source": osm_id_to_dense[source_osm_id],
        "target": osm_id_to_dense[target_osm_id],
        "edges": edges,
    }