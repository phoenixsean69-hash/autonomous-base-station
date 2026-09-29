from __future__ import annotations

import json
import os
import urllib.request


BASE = os.environ.get(
    "ABS_NETWORK_HTTP",
    "http://127.0.0.1:8000",
).rstrip("/")


def main() -> int:
    url = (
        BASE +
        "/base-station/telemetry"
    )

    with urllib.request.urlopen(
        url,
        timeout=2.0,
    ) as response:
        data = json.loads(
            response.read().decode(
                "utf-8"
            )
        )

    required = [
        "schema",
        "active_calls",
        "traffic_load_pct",
        "access",
        "backhaul",
    ]

    missing = [
        key
        for key in required
        if key not in data
    ]

    if missing:
        raise RuntimeError(
            "Missing fields: " +
            ", ".join(missing)
        )

    if (
        data["schema"] !=
        "abs.network.telemetry.v1"
    ):
        raise RuntimeError(
            "Unexpected schema: " +
            str(data["schema"])
        )

    print()
    print(
        "BASE-STATION NETWORK FEED"
    )
    print(
        json.dumps(
            data,
            indent=2,
        )
    )
    print()
    print(
        "[PASS] Network simulator is exposing "
        "base-station telemetry."
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(
        main()
    )
