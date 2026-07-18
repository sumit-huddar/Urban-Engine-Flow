"""
agent.py
------------------------------------------------------------------
Urban Flow AI Engine -- the LLM tool-calling layer.

Terminal loop: plain English question -> Gemini picks the right
algorithm -> extracts source/target -> C++ engine runs it ->
Gemini explains the result in plain English.

Run: python agent.py
Requires: GOOGLE_API_KEY set as an environment variable.
------------------------------------------------------------------
"""

import json
import osmnx as ox
from google import genai
from google.genai import types

from engine import run_engine, EngineError
from osm_to_engine import osmnx_graph_to_engine_json

# ------------------------------------------------------------------
# CONFIG -- edit these for your area
# ------------------------------------------------------------------
CENTER_POINT      = (26.510800755672246, 80.2274508869843)
DOWNLOAD_RADIUS_M = 2000

LANDMARKS = {
    "hall 6":     (26.50534996924456, 80.2346067634557),
    "hall 12":    (26.510800755672246, 80.2274508869843),
    "mt canteen": (26.512411033250377, 80.23045501385931),
    # Coordinates below geocoded from OpenStreetMap (ox.geocode) and
    # verified to fall inside the 2000 m download radius.
    "hall 1":     (26.511213, 80.232813),
    "hall 2":     (26.510808, 80.230211),
    "hall 3":     (26.508658, 80.229923),
    "hall 4":     (26.511547, 80.233168),
    "hall 5":     (26.509551, 80.227883),
    "lhc":        (26.511278, 80.232987),  # Lecture Hall Complex
    "airstrip":   (26.519668, 80.233173),
    # add more as needed, all keys must be lowercase
}

# Use the "latest flash" alias rather than a pinned version so the agent
# keeps working when Google retires a specific model (e.g. gemini-2.5-flash
# became unavailable to new users). Pin to a dated model if you need
# reproducibility.
MODEL_NAME = "gemini-flash-latest"

# ------------------------------------------------------------------
# TOOL DEFINITIONS -- using types.FunctionDeclaration (free tier)
# ------------------------------------------------------------------
dijkstra_fn = types.FunctionDeclaration(
    name="find_shortest_route",
    description=(
        "Finds the shortest/fastest route between two locations in the "
        "city road network based on real road distance. Use for questions "
        "about the quickest way to get somewhere or distance between points."
    ),
    parameters_json_schema={
        "type": "object",
        "properties": {
            "source": {"type": "string", "description": "Starting location name"},
            "target": {"type": "string", "description": "Destination location name"},
        },
        "required": ["source", "target"],
    },
)

dinic_fn = types.FunctionDeclaration(
    name="find_max_traffic_flow",
    description=(
        "Finds the maximum sustainable traffic flow (vehicles/hour) the "
        "road network can carry between two locations. Use for questions "
        "about traffic capacity or how much traffic a route can handle."
    ),
    parameters_json_schema={
        "type": "object",
        "properties": {
            "source": {"type": "string", "description": "Starting location name"},
            "target": {"type": "string", "description": "Destination location name"},
        },
        "required": ["source", "target"],
    },
)

min_cut_fn = types.FunctionDeclaration(
    name="find_traffic_bottleneck",
    description=(
        "Identifies the roads that form the traffic bottleneck between two "
        "locations -- the weakest link limiting flow. Use for questions about "
        "where congestion occurs, which roads are critical, or network vulnerability."
    ),
    parameters_json_schema={
        "type": "object",
        "properties": {
            "source": {"type": "string", "description": "Starting location name"},
            "target": {"type": "string", "description": "Destination location name"},
        },
        "required": ["source", "target"],
    },
)

# All three wrapped in one Tool object
TOOL = types.Tool(function_declarations=[dijkstra_fn, dinic_fn, min_cut_fn])

TOOL_TO_ALGORITHM = {
    "find_shortest_route":     "dijkstra",
    "find_max_traffic_flow":   "dinic",
    "find_traffic_bottleneck": "min_cut",
}


class LocationNotFoundError(Exception):
    pass


def resolve_location(name: str, G) -> int:
    """Resolve a landmark name to a real graph node ID."""
    key = name.strip().lower()
    if key not in LANDMARKS:
        known = ", ".join(LANDMARKS.keys())
        raise LocationNotFoundError(
            f"Don't know '{name}'. Known landmarks: {known}"
        )
    lat, lng = LANDMARKS[key]
    # nearest_nodes: X=longitude, Y=latitude (opposite of Google Maps order)
    return ox.distance.nearest_nodes(G, X=lng, Y=lat)


def handle_query(question: str, G, client) -> str:
    """Full pipeline for one user question."""

    # Step 1: Gemini picks which tool to call + extracts source/target
    response = client.models.generate_content(
        model=MODEL_NAME,
        contents=question,
        config=types.GenerateContentConfig(tools=[TOOL]),
    )

    # Check if the model made a function call
    if not response.function_calls:
        # Model answered in plain text (off-topic question etc.)
        return response.text

    fc = response.function_calls[0]
    algorithm = TOOL_TO_ALGORITHM.get(fc.name)
    if not algorithm:
        return f"Model called an unrecognized tool: {fc.name}"

    source_name = fc.args.get("source", "")
    target_name = fc.args.get("target", "")

    # Step 2: Resolve location names -> real graph node IDs
    try:
        source_node = resolve_location(source_name, G)
        target_node = resolve_location(target_name, G)
    except LocationNotFoundError as ex:
        return str(ex)

    # Step 3: Run the C++ engine
    query = osmnx_graph_to_engine_json(
        G, source_node, target_node, algorithm=algorithm
    )
    try:
        result = run_engine(query)
    except EngineError as ex:
        return f"Engine error: {ex}"

    # Step 4: Ask Gemini to explain the raw result in plain English
    explanation_prompt = (
        f"The user asked: '{question}'\n"
        f"The {algorithm} algorithm on the city road network returned:\n"
        f"{json.dumps(result, indent=2)}\n\n"
        f"Explain this in 2-3 plain English sentences a non-technical "
        f"person would understand. If distance, convert meters to km. "
        f"If flow, describe as vehicles per hour."
    )
    explanation = client.models.generate_content(
        model=MODEL_NAME,
        contents=explanation_prompt,
    )
    return explanation.text


def main():
    print("Urban Flow AI Engine -- loading road network...")
    G = ox.graph_from_point(CENTER_POINT, dist=DOWNLOAD_RADIUS_M, network_type="drive")
    print(f"Loaded {G.number_of_nodes()} nodes, {G.number_of_edges()} edges.")
    print(f"Known landmarks: {', '.join(LANDMARKS.keys())}")
    print("Type a question (or 'quit' to exit).\n")

    client = genai.Client()  # reads GOOGLE_API_KEY from environment

    while True:
        try:
            question = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nGoodbye.")
            break
        if not question:
            continue
        if question.lower() in ("quit", "exit", "q"):
            break

        answer = handle_query(question, G, client)
        print(f"\n{answer}\n")


if __name__ == "__main__":
    main()