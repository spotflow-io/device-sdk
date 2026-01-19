#!/usr/bin/env python3
"""
Generate GitHub Actions matrix from boards.yml.

This script reads the board configuration from boards.yml and generates
a matrix JSON for GitHub Actions workflows.
"""

import json
import os
import yaml
from pathlib import Path
from typing import Dict, Any, List


def get_property(
    key: str,
    board: Dict[str, Any],
    vendor: Dict[str, Any],
    defaults: Dict[str, Any],
    default: Any = None,
) -> Any:
    """Get property with priority: board > vendor > defaults > default."""
    return board.get(key, vendor.get(key, defaults.get(key, default)))


def load_yaml(filepath: Path) -> Dict[str, Any]:
    with open(filepath, "r") as f:
        return yaml.safe_load(f)


def generate_matrix(
    boards_yml_path: Path, default_built_samples: List[str] = []
) -> Dict[str, Any]:
    """Generate the GitHub Actions matrix from boards configuration."""
    config = load_yaml(boards_yml_path)
    defaults = config.get("defaults", {})
    matrix_entries = []

    for vendor in config["vendors"]:
        for board in vendor["boards"]:
            build_samples = get_property("build_samples", board, vendor, defaults, [])

            missing_default_samples = default_built_samples - build_samples
            build_samples = build_samples + missing_default_samples

            for sample in build_samples:
                entry = {
                    "board": board.get("board", board["id"]),
                    "sample": sample,
                    "manifest": get_property("manifest", board, vendor, defaults),
                    "sdk_version": get_property("sdk_version", board, vendor, defaults),
                    "sdk_toolchain": get_property(
                        "sdk_toolchain", board, vendor, defaults
                    ),
                    "blob": get_property("blob", board, vendor, defaults, ""),
                    "build_extra_args": get_property(
                        "build_extra_args", board, vendor, defaults, ""
                    ),
                }
                matrix_entries.append(entry)

    return {"include": matrix_entries}


def write_github_output(key: str, value: str):
    """Write output to GitHub Actions GITHUB_OUTPUT file."""
    with open(os.environ["GITHUB_OUTPUT"], "a") as f:
        f.write(f"{key}={value}\n")


def main():
    script_dir = Path(__file__).parent
    boards_yml_path = script_dir / "boards.yml"

    build_logs_for_all = os.environ.get("BUILD_LOGS_FOR_ALL_BOARDS", "False") == "True"
    default_built_samples = ["logs"] if build_logs_for_all else []

    print(f"Loading boards configuration from {boards_yml_path}")
    print(f"Build logging sample for all boards: {build_logs_for_all}")
    matrix = generate_matrix(boards_yml_path, default_built_samples)

    matrix_json = json.dumps(matrix)
    write_github_output("matrix", matrix_json)

    print(f"Generated matrix with {len(matrix['include'])} entries")


if __name__ == "__main__":
    main()
