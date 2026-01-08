#!/usr/bin/env python3
"""
Generate quickstart.json from boards.yml and manifest files.

This script reads the board configuration from boards.yml, extracts metadata
from manifest files, and generates a quickstart.json file for documentation
and tooling purposes.
"""

import json
import yaml
from pathlib import Path
from typing import Dict, Any


def load_yaml(filepath: Path) -> Dict[str, Any]:
    with open(filepath, "r") as f:
        return yaml.safe_load(f)


def extract_spotflow_path(manifest_filepath: Path) -> str:
    manifest_data = load_yaml(manifest_filepath)
    return manifest_data.get("manifest", {}).get("self", {}).get("path", "")


def get_board_property(
    board: Dict[str, Any], vendor: Dict[str, Any], key: str, default: Any = None
) -> Any:
    return board.get(key, vendor.get(key, default))


def compute_board_prefix(board_value: str) -> str:
    return board_value.split("/")[0]


def compute_sample_device_id(board_value: str) -> str:
    """
    Example: 'thingy53/nrf5340/cpuapp/ns' -> 'thingy53-001'
    Example: 'frdm_k64f' -> 'frdm-k64f-001'
    """
    prefix = compute_board_prefix(board_value)
    prefix = prefix.replace("_", "-")
    return f"{prefix}-001"


def compute_zephyr_docs_url(vendor: str, board_value: str) -> str:
    board_prefix = compute_board_prefix(board_value)
    return f"https://docs.zephyrproject.org/latest/boards/{vendor}/{board_prefix}/doc/index.html"


def transform_board(
    board: Dict[str, Any], vendor: Dict[str, Any], spotflow_paths: Dict[str, str]
) -> Dict[str, Any]:
    """
    Transform a board entry by applying property inheritance and computing
    derived properties.
    """
    board_value = board.get("board", board["id"])

    manifest = get_board_property(board, vendor, "manifest")
    spotflow_path = spotflow_paths.get(manifest, "")

    result = {
        "id": board["id"],
        "name": board["name"],
        "board": board_value,
        "manifest": manifest,
        "spotflow_path": spotflow_path,
        "sdk_version": get_board_property(board, vendor, "sdk_version"),
        "sdk_toolchain": get_board_property(board, vendor, "sdk_toolchain"),
        "connection": get_board_property(board, vendor, "connection", []),
        "zephyr_docs": compute_zephyr_docs_url(vendor["vendor"], board_value),
        "sample_device_id": compute_sample_device_id(board_value),
    }

    blob = get_board_property(board, vendor, "blob", "")
    if blob:
        result["blob"] = blob

    build_extra_args = get_board_property(board, vendor, "build_extra_args", "")
    if build_extra_args:
        result["build_extra_args"] = build_extra_args

    return result


def generate_quickstart(
    boards_config: Dict[str, Any], spotflow_paths: Dict[str, str]
) -> Dict[str, Any]:
    """Generate the complete quickstart.json structure."""

    # Hard-coded esp_idf section
    esp_idf = {
        "boards": [
            {"name": "ESP32", "target": "esp32"},
            {"name": "ESP32-C3", "target": "esp32c3"},
        ]
    }

    # Generate NCS section (filter boards with vendor="nordic")
    ncs_boards = []
    for vendor in boards_config["vendors"]:
        if vendor.get("vendor") == "nordic":
            for board in vendor["boards"]:
                transformed = transform_board(board, vendor, spotflow_paths)
                ncs_boards.append(transformed)

    ncs = {"boards": ncs_boards}

    # Generate Zephyr section (all vendors with nested structure)
    zephyr_vendors = []
    for vendor in boards_config["vendors"]:
        vendor_boards = []
        for board in vendor["boards"]:
            transformed = transform_board(board, vendor, spotflow_paths)
            vendor_boards.append(transformed)

        zephyr_vendors.append({"name": vendor["name"], "boards": vendor_boards})

    zephyr = {"vendors": zephyr_vendors}

    return {"esp_idf": esp_idf, "ncs": ncs, "zephyr": zephyr}


def main():
    script_dir = Path(__file__).parent

    boards_yml_path = script_dir / "boards.yml"
    manifests_dir = script_dir.parent / "manifests"
    output_path = script_dir / "quickstart.json"

    print(f"Loading boards configuration from {boards_yml_path}")
    boards_config = load_yaml(boards_yml_path)

    print(f"Loading manifest files from {manifests_dir}")
    spotflow_paths = {}
    for manifest_file in manifests_dir.glob("*.yml"):
        manifest_name = manifest_file.name
        spotflow_path = extract_spotflow_path(manifest_file)
        spotflow_paths[manifest_name] = spotflow_path
        print(f"  {manifest_name}: spotflow_path = {spotflow_path}")

    print("Generating quickstart.json...")
    quickstart_data = generate_quickstart(boards_config, spotflow_paths)

    print(f"Writing output to {output_path}")
    with open(output_path, "w") as f:
        json.dump(quickstart_data, f, indent=4)

    print("Done!")


if __name__ == "__main__":
    main()
