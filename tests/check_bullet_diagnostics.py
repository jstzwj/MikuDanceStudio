"""Exercise real Bullet tracing in fresh processes and compare simulation bits."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    fixture = str(Path(sys.argv[1]).resolve())
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("OPENMMD_")}
    with tempfile.TemporaryDirectory(prefix="mds-bullet-trace-") as directory:
        root = Path(directory)
        baseline = subprocess.run([fixture], env=env, capture_output=True, check=True)
        for spring, solver in [(True, False), (False, True), (True, True)]:
            spring_path = root / f"spring-{spring}-{solver}.log"
            solver_path = root / f"solver-{spring}-{solver}.jsonl"
            traced_env = dict(env)
            if spring:
                traced_env["OPENMMD_DUMP_SPRING"] = str(spring_path)
            if solver:
                traced_env["OPENMMD_DUMP_RESOLVE"] = str(solver_path)
                traced_env["OPENMMD_DUMP_SOLVES"] = "32"
            traced = subprocess.run([fixture], env=traced_env, capture_output=True, check=True)
            require(traced.stdout == baseline.stdout,
                    f"Tracing changes motor or simulation bits: spring={spring}, solver={solver}")
            if spring:
                lines = spring_path.read_text().splitlines()
                require(len(lines) > 32, "Spring trace was not exercised")
                for line in lines:
                    require("ba=1234567 bb=2345678" in line,
                            f"Incorrect spring body IDs: {line}")
                expected = next(line.removeprefix("expected_vel ") for line in
                                baseline.stdout.decode().splitlines()
                                if line.startswith("expected_vel "))
                require(expected in lines, "Spring trace does not contain the actual body velocities")
            if solver:
                # Legacy events are JSON-like and can contain trailing commas
                # and unlabelled vector lines. Read only the diagnostic fields.
                events = [line for line in solver_path.read_text().splitlines()
                          if line.startswith("{")]
                require(events, "Solver trace was not exercised")
                ids = [int(value) for line in events for value in
                       re.findall(r'"(?:id|id_a|id_b)":(-?\d+)', line)]
                require({1234567, 2345678}.issubset(ids), "Solver trace missed dynamic body IDs")
                require(set(ids) <= {1234567, 2345678, 3456789, -1},
                        f"Solver trace contains incorrect body IDs: {set(ids)}")
                require(any('"kind":"joint"' in event for event in events), "No joint trace")
                require(any('"kind":"contact"' in event for event in events), "No contact trace")
        print("Tracing preserves all 32 simulation frames and reports actual body IDs/velocities.")


if __name__ == "__main__":
    main()
