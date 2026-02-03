# Continuous Integration for Zephyr Module

This directory contains the continuous integration (CI) configuration and related tooling for the Spotflow Zephyr module.

## Board List

`boards.yml` is the central configuration file defining all supported vendors and boards.
The following reasons motivated the creation of this file:

- We wanted to have a single source of truth for the boards we support and present them to the users only when we know we can (at least) compile the module for them.
- Many dependencies affect the compilation process, such as the Zephyr version, the Zephyr SDK version, downloaded blobs, etc.
  Capturing all the dependencies of each board allows us to set up the user's workspace consistently with the CI pipeline.
- Neither our CI pipeline nor the user should download dependencies that are not needed for the selected board.
  Therefore, we should specify only the necessary dependencies for each board.

As the single source of truth, `boards.yml` is used in two workflows:

- `zephyr.yml` uses `generate_matrix.py` to generate the matrix of boards and samples to build.
- `quickstart.yml` uses `generate_quickstart_json.py` to generate and publish [`quickstart.json`](https://downloads.spotflow.io/quickstart.json), which is itself used for two purposes:
   - The quickstart guides in the portal and documentation use it to list the boards the user can select from.
   - `spotflowup.sh` and `spotflowup.ps1` (see [below](#workspace-setup-scripts)) use it to obtain the workspace setup details for the selected board.

The boards are grouped by vendors.
Because boards from the same vendor often share similar properties, we can define vendor defaults that are inherited by the boards.
Furthermore, certain properties are common to most vendors (e.g., the Zephyr SDK version), so they are defined as global defaults.

Board properties:

| Property | Required | Description |
|----------|----------|-------------|
| `id` | Yes | Our unique identifier for the board |
| `name` | Yes | Human-readable board name |
| `board` | No | Zephyr board target (defaults to `id` if not specified) |
| `manifest` | Yes | West manifest file to use (see [`manifests`](../manifests) directory) |
| `sdk_version` | Yes | Zephyr SDK version |
| `sdk_toolchain` | No | SDK toolchain to install (all toolchains are installed if not specified) |
| `blob` | No | Binary blob to download (e.g., `hal_espressif`, `hal_nxp`) |
| `connection` | Yes | Supported connection methods (Wi-Fi, Ethernet) |
| `build_samples` | No | Samples to build in CI (e.g., `["logs"]`) by default (building the logs sample can be manually triggered for all boards) |
| `build_extra_args` | No | Additional arguments for `west build` |
| `callout` | No | HTML callout displayed in quickstart |

## Workspace Setup Scripts

`spotflowup.sh` and `spotflowup.ps1` automate the west workspace setup so that the user can easily compile our samples for the selected board.

Apart from differences in certain Bash and PowerShell commands, the scripts have the same functionality.
Operating systems supported:

- `spotflowup.sh` - Linux and macOS
- `spotflowup.ps1` - Linux, macOS, and Windows

Instead of running the scripts, you can set up the workspace manually using the following steps:

**1. Install west in a new Python virtual environment:**


```bash
mkdir spotflow-ws
cd spotflow-ws

python -m venv .venv

# .venv/Scripts/Activate.ps1 in PowerShell
.venv/bin/activate

pip install west
```

**2. Initialize the west workspace:**

```bash
west init --manifest-url https://github.com/spotflow-io/device-sdk --manifest-file zephyr/manifests/west-zephyr.yml .
west update --fetch-opt=--depth=1 --narrow
west packages pip --install
```

The manifest `west-zephyr.yml` references the latest stable Zephyr version with all dependencies.
This setup builds samples on most boards, but downloads many unneeded modules.
Therefore, we prepared [custom manifests for each vendor](../manifests) containing only the necessary modules.
The setup scripts use the appropriate manifest based on the selected board.

> 💡 Use the manifest `west-ncs.yml` if you're using the nRF Connect SDK instead of "vanilla" Zephyr.

Certain boards require additional blobs to be downloaded, for example:

```bash
west blobs fetch hal_nxp --auto-accept
```

**3. Install the Zephyr SDK:**

Install the latest stable Zephyr SDK using the following command.
You can select the specific toolchain needed for the selected board, so that you don't have to download all toolchains:

```bash
west sdk install --version 0.17.4 --toolchains arm-zephyr-eabi
```

**OR** when working with the nRF Connect SDK, install the toolchain using the following command:

```bash
nrfutil sdk-manager toolchain install --ncs-version 3.2.1
```

**4. Add placeholders for the configuration options:**

Add the following placeholders to the `prj.conf` file of the Zephyr sample `logs` in the Spotflow module:

```
CONFIG_NET_WIFI_SSID="<Your Wi-Fi SSID>"
CONFIG_NET_WIFI_PASSWORD="<Your Wi-Fi Password>"

CONFIG_SPOTFLOW_DEVICE_ID="device-001"
CONFIG_SPOTFLOW_INGEST_KEY="<Your Spotflow Ingest Key>"
```

The first two placeholders are only needed if the board supports Wi-Fi.
