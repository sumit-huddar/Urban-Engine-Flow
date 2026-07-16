import osmnx as ox
from osm_to_engine import osmnx_graph_to_engine_json
from engine import run_engine

G = ox.graph_from_point((26.51232107843698, 80.2338149925844), dist=2500, network_type="drive")
source_node = ox.distance.nearest_nodes(G,X=80.2338149925844, Y=26.51232107843698)   # X=longitude, Y=latitude — note the order!
target_node = ox.distance.nearest_nodes(G, X=80.22752443082155 , Y=26.510990040983128)

query = osmnx_graph_to_engine_json(G, source_node, target_node, algorithm="min_cut")
result = run_engine(query)
print(result)
