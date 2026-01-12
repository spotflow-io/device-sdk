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


# Placeholder value for board name in "Other" boards
BOARD_PLACEHOLDER = "<board>"


def load_yaml(filepath: Path) -> Dict[str, Any]:
    with open(filepath, "r") as f:
        return yaml.safe_load(f)


def extract_spotflow_path(manifest_filepath: Path) -> str:
    manifest_data = load_yaml(manifest_filepath)
    return manifest_data.get("manifest", {}).get("self", {}).get("path", "")


def get_board_property(
    key: str,
    board: Dict[str, Any],
    vendor: Dict[str, Any],
    defaults: Dict[str, Any] = None,
    default: Any = None,
) -> Any:
    """
    Get a property value with priority: board > vendor > global defaults > default.
    """
    if defaults is None:
        defaults = {}
    return board.get(key, vendor.get(key, defaults.get(key, default)))


def compute_board_prefix(board_value: str) -> str:
    return board_value.split("/")[0]


def compute_sample_device_id(board_value: str, vendor_id: str = None) -> str:
    """
    Example: 'thingy53/nrf5340/cpuapp/ns' -> 'thingy53-001'
    Example: 'frdm_k64f' -> 'frdm-k64f-001'
    Example: BOARD_PLACEHOLDER with vendor_id='nxp' -> 'nxp-device-001'
    Example: BOARD_PLACEHOLDER with vendor_id=None -> 'device-001'
    """
    if board_value == BOARD_PLACEHOLDER:
        if vendor_id:
            return f"{vendor_id}-device-001"
        return "device-001"

    prefix = compute_board_prefix(board_value)
    prefix = prefix.replace("_", "-")
    return f"{prefix}-001"


def compute_zephyr_docs_url(vendor: str, board_value: str) -> str:
    board_prefix = compute_board_prefix(board_value)
    return f"https://docs.zephyrproject.org/latest/boards/{vendor}/{board_prefix}/doc/index.html"


def transform_board(
    board: Dict[str, Any],
    vendor: Dict[str, Any],
    spotflow_paths: Dict[str, str],
    defaults: Dict[str, Any] = None,
    is_other_board: bool = False,
) -> Dict[str, Any]:
    """
    Transform a board entry by applying property inheritance and computing
    derived properties.
    """
    if defaults is None:
        defaults = {}

    board_value = board.get("board", board["id"])

    manifest = get_board_property("manifest", board, vendor, defaults)
    spotflow_path = spotflow_paths.get(manifest, "")

    vendor_id = vendor.get("vendor", None)

    # For "Other" boards, always use ["wifi", "ethernet"] connection
    if is_other_board:
        connection = ["wifi", "ethernet"]
    else:
        connection = get_board_property("connection", board, vendor, defaults, [])

    result = {
        "id": board["id"],
        "name": board["name"],
        "board": board_value,
        "manifest": manifest,
        "spotflow_path": spotflow_path,
        "sdk_version": get_board_property("sdk_version", board, vendor, defaults),
        "connection": connection,
        "sample_device_id": compute_sample_device_id(board_value, vendor_id),
    }

    # Only include sdk_toolchain if it exists in the hierarchy
    sdk_toolchain = get_board_property("sdk_toolchain", board, vendor, defaults)
    if sdk_toolchain:
        result["sdk_toolchain"] = sdk_toolchain

    # Only include zephyr_docs if board_value is not a placeholder
    if board_value != BOARD_PLACEHOLDER:
        result["zephyr_docs"] = compute_zephyr_docs_url(vendor_id, board_value)

    callout = get_board_property("callout", board, vendor, defaults, "")
    if callout:
        result["callout"] = callout

    blob = get_board_property("blob", board, vendor, defaults, "")
    if blob:
        result["blob"] = blob

    build_extra_args = get_board_property(
        "build_extra_args", board, vendor, defaults, ""
    )
    if build_extra_args:
        result["build_extra_args"] = build_extra_args

    return result


def create_other_board(vendor_id: str = None) -> Dict[str, Any]:
    return {
        "id": f"other_{vendor_id}" if vendor_id else "other",
        "name": "Other",
        "board": BOARD_PLACEHOLDER,
    }


def generate_quickstart(
    boards_config: Dict[str, Any], spotflow_paths: Dict[str, str]
) -> Dict[str, Any]:
    """Generate the complete quickstart.json structure."""

    # Extract global defaults
    defaults = boards_config.get("defaults", {})

    # Hard-coded esp_idf section
    esp_idf = {
        "boards": [
            {"name": "ESP32", "target": "esp32"},
            {"name": "ESP32-C3", "target": "esp32c3"},
        ]
    }

    # Generate NCS and Zephyr boards (the latter are nested in vendors)
    ncs_boards = []
    zephyr_vendors = []
    for vendor in boards_config["vendors"]:
        vendor_boards = []
        for board in vendor["boards"]:
            transformed = transform_board(board, vendor, spotflow_paths, defaults)
            vendor_boards.append(transformed)

        # Add "Other" board for this vendor
        other_board = create_other_board(vendor["vendor"])
        transformed_other = transform_board(
            other_board, vendor, spotflow_paths, defaults, is_other_board=True
        )
        vendor_boards.append(transformed_other)

        if vendor["vendor"] == "nordic":
            ncs_boards.extend(vendor_boards)

        zephyr_vendors.append({"name": vendor["name"], "boards": vendor_boards})

    # Add standalone "Other" vendor with "Other" board
    other_board = create_other_board()
    transformed_other = transform_board(
        other_board, {}, spotflow_paths, defaults, is_other_board=True
    )
    zephyr_vendors.append({"name": "Other", "boards": [transformed_other]})

    ncs = {"boards": ncs_boards}
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
