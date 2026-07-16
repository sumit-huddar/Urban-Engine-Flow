"""
engine.py
------------------------------------------------------------------
Python <-> C++ bridge for the Urban Flow AI Engine.

If you're thinking in C++ terms: this file's job is roughly what
calling system() or popen() does -- it launches your compiled
`engine` binary as a separate process, writes a string to its
stdin (like piping into a program on the command line), and reads
back whatever it printed to stdout. The "json.dumps" / "json.loads"
calls below are doing exactly what nlohmann::json's .dump() and
json::parse() do in your C++ code -- converting between a Python
dict (like a C++ struct/map) and a JSON string.

This file has ONE job: take a query, run the binary, return a dict.
It knows nothing about algorithms, LLMs, or anything else -- that's
agent.py's job later.
------------------------------------------------------------------
"""

import json
import subprocess
from pathlib import Path

# ------------------------------------------------------------------
# Resolve the path to the compiled binary RELATIVE TO THIS FILE,
# not relative to wherever the script happens to be run from.
# This is the "build-order decision" flagged earlier -- it's what
# makes this work the same way on your machine and your mentees'
# machines, regardless of which folder they run agent.py from.
#
# Path(__file__) is "the path to this very file" (engine.py).
# .parent is "the folder containing this file" -- like using
# argv[0] in C++ to find your own executable's directory.
# ------------------------------------------------------------------
ENGINE_BINARY_PATH = Path(__file__).parent / "engine"


class EngineError(Exception):
    """Raised when the C++ engine reports an error in its JSON output."""
    pass


def run_engine(query: dict) -> dict:
    """
    Run the compiled C++ engine with the given query and return its
    parsed JSON result as a Python dict.

    `query` should look like what you've been hand-typing as JSON
    all along, e.g.:
        {
            "algorithm": "dijkstra",
            "num_nodes": 5,
            "source": 0,
            "target": 4,
            "edges": [...]
        }

    Raises EngineError if the binary's output contains an "error" key,
    or if the binary couldn't be run / produced unparseable output.
    """

    # --- Step 1: dict -> JSON string ---
    # Same as g.dump() on a nlohmann::json object in your C++ code.
    input_json = json.dumps(query)

    # --- Step 2: run the binary, piping input_json into its stdin ---
    # subprocess.run() is Python's version of "spawn a process and
    # wait for it to finish." The arguments matter:
    #   - [str(ENGINE_BINARY_PATH)]   -> what program to run (like argv[0])
    #   - input=input_json            -> text written to the process's stdin
    #   - capture_output=True         -> capture its stdout/stderr instead
    #                                    of letting them print to your terminal
    #   - text=True                   -> give/take strings, not raw bytes
    #   - timeout=10                  -> safety net: don't hang forever if
    #                                    the binary gets stuck (this is the
    #                                    kind of bug we hit with the
    #                                    un-reset level array earlier!)
    try:
        proc = subprocess.run(
            [str(ENGINE_BINARY_PATH)],
            input=input_json,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except FileNotFoundError:
        raise EngineError(
            f"Could not find compiled engine binary at {ENGINE_BINARY_PATH}. "
            f"Did you run `g++ -O2 -std=c++17 engine.cpp -o engine` in this folder?"
        )
    except subprocess.TimeoutExpired:
        raise EngineError("Engine process timed out after 10 seconds.")

    raw_stdout = proc.stdout.strip()

    # --- Step 3: handle the case where the binary printed nothing at all ---
    # (this is the "exit code 0, but no output" scenario from your old
    # unknown-algorithm bug -- now fixed in engine.cpp, but it's still
    # good defensive practice to check for it here too)
    if not raw_stdout:
        raise EngineError(
            f"Engine produced no output. Exit code: {proc.returncode}. "
            f"stderr: {proc.stderr.strip()}"
        )

    # --- Step 4: JSON string -> dict ---
    # Same as json::parse() in your C++ code. If the binary somehow
    # printed something that isn't valid JSON, this is where you'd
    # find out -- better to fail loudly here than pass garbage upstream.
    try:
        result = json.loads(raw_stdout)
    except json.JSONDecodeError as ex:
        raise EngineError(f"Engine output was not valid JSON: {raw_stdout!r} ({ex})")

    # --- Step 5: check for the engine's own reported errors ---
    # Recall engine.cpp's two failure shapes: a parse-failure error
    # object, or the "unknown algorithm" error object. Both look the
    # same from here -- a dict with an "error" key. We surface both
    # the same way: raise, so callers can't accidentally treat a
    # failed query as a successful one.
    if "error" in result:
        raise EngineError(result["error"])

    return result


# ------------------------------------------------------------------
# Quick manual test -- run this file directly (python engine.py)
# to sanity-check the bridge against four known answers, the same
# way you've been testing engine.cpp by hand all along.
# ------------------------------------------------------------------
if __name__ == "__main__":
    tests = [
        {
            "name": "Dijkstra (expect distance 6, path [0,3,4])",
            "query": {
                "algorithm": "dijkstra",
                "num_nodes": 5,
                "source": 0,
                "target": 4,
                "edges": [
                    {"from": 0, "to": 1, "weight": 2, "undirected": True},
                    {"from": 1, "to": 2, "weight": 2, "undirected": True},
                    {"from": 0, "to": 3, "weight": 5, "undirected": True},
                    {"from": 3, "to": 4, "weight": 1, "undirected": True},
                    {"from": 2, "to": 4, "weight": 3, "undirected": True},
                ],
            },
        },
        {
            "name": "Dinic's (expect max_flow 23)",
            "query": {
                "algorithm": "dinic",
                "num_nodes": 6,
                "source": 0,
                "target": 5,
                "edges": [
                    {"from": 0, "to": 1, "capacity": 16},
                    {"from": 0, "to": 2, "capacity": 13},
                    {"from": 1, "to": 2, "capacity": 10},
                    {"from": 1, "to": 3, "capacity": 12},
                    {"from": 2, "to": 1, "capacity": 4},
                    {"from": 2, "to": 4, "capacity": 14},
                    {"from": 3, "to": 2, "capacity": 9},
                    {"from": 3, "to": 5, "capacity": 20},
                    {"from": 4, "to": 3, "capacity": 7},
                    {"from": 4, "to": 5, "capacity": 4},
                ],
            },
        },
        {
            "name": "Min-Cut (expect min_cut_value 23, 3 real cut edges)",
            "query": {
                "algorithm": "min_cut",
                "num_nodes": 6,
                "source": 0,
                "target": 5,
                "edges": [
                    {"from": 0, "to": 1, "capacity": 16},
                    {"from": 0, "to": 2, "capacity": 13},
                    {"from": 1, "to": 2, "capacity": 10},
                    {"from": 1, "to": 3, "capacity": 12},
                    {"from": 2, "to": 1, "capacity": 4},
                    {"from": 2, "to": 4, "capacity": 14},
                    {"from": 3, "to": 2, "capacity": 9},
                    {"from": 3, "to": 5, "capacity": 20},
                    {"from": 4, "to": 3, "capacity": 7},
                    {"from": 4, "to": 5, "capacity": 4},
                ],
            },
        },
        {
            "name": "Unknown algorithm (expect EngineError raised)",
            "query": {"algorithm": "bogus", "num_nodes": 1, "source": 0, "target": 0, "edges": []},
        },
    ]

    for t in tests:
        print(f"--- {t['name']} ---")
        try:
            print(run_engine(t["query"]))
        except EngineError as ex:
            print(f"EngineError raised (as expected for the last test): {ex}")
        print()
