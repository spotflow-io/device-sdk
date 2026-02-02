#!/usr/bin/env bash
#
# Spotflow Device SDK workspace setup script for Zephyr and nRF Connect SDK.
#
# This script automates the creation of a west workspace for building
# Spotflow Device SDK samples on supported boards.
#
# Usage:
#   ./spotflowup.sh --zephyr --board frdm_rw612
#   ./spotflowup.sh --ncs --board nrf7002dk
#   ./spotflowup.sh --zephyr --board frdm_rw612 --workspace-folder ~/my-workspace --auto-confirm

set -euo pipefail

# Configuration
readonly DEFAULT_QUICKSTART_JSON_URL="https://downloads.spotflow.io/quickstart.json"
readonly MANIFEST_BASE_URL="https://github.com/spotflow-io/device-sdk"

# Global variables
sdk_type=""
board=""
workspace_folder=""
spotflow_revision=""
quickstart_json_url=""
github_token=""
auto_confirm=false
sdk_display_type=""

# ANSI color codes
readonly COLOR_RESET='\033[0m'
readonly COLOR_CYAN='\033[0;36m'
readonly COLOR_BLUE='\033[0;34m'
readonly COLOR_GREEN='\033[0;32m'
readonly COLOR_YELLOW='\033[0;33m'
readonly COLOR_RED='\033[0;31m'
readonly COLOR_MAGENTA='\033[0;35m'
readonly COLOR_WHITE='\033[1;37m'
readonly COLOR_GRAY='\033[0;37m'
readonly COLOR_DARK_GRAY='\033[1;30m'

# Interaction helper functions

write_step() {
    echo -e "\n${COLOR_CYAN}>${COLOR_WHITE} $1${COLOR_RESET}" >&2
}

write_info() {
    echo -e "  ${COLOR_BLUE}[i]${COLOR_GRAY} $1${COLOR_RESET}" >&2
}

write_success() {
    echo -e "  ${COLOR_GREEN}[+]${COLOR_WHITE} $1${COLOR_RESET}" >&2
}

write_warning() {
    echo -e "  ${COLOR_YELLOW}[!] $1${COLOR_RESET}" >&2
}

write_error() {
    echo -e "  ${COLOR_RED}[x] $1${COLOR_RESET}" >&2
}

exit_with_error() {
    local message="$1"
    local details="${2:-}"
    local discord_url="https://discord.com/channels/1372202003635114125/1379411086163574864"

    echo "" >&2
    write_error "$message"
    if [[ -n "$details" ]]; then
        echo -e "  ${COLOR_DARK_GRAY}Details: $details${COLOR_RESET}" >&2
    fi
    echo "" >&2
    echo -e "${COLOR_GRAY}If you believe this is a bug, please report it at:${COLOR_RESET}" >&2
    echo -e "  ${COLOR_CYAN}https://github.com/spotflow-io/device-sdk/issues${COLOR_RESET}" >&2
    echo "" >&2
    echo -e "${COLOR_GRAY}You can also contact us on Discord:${COLOR_RESET}" >&2
    echo -e "  ${COLOR_CYAN}${discord_url}${COLOR_RESET}" >&2
    echo "" >&2
    exit 1
}

confirm_action() {
    local message="$1"
    local default_yes="${2:-true}"

    if [[ "$auto_confirm" == true ]]; then
        write_info "$message (auto-confirmed)"
        return 0
    fi

    local suffix
    if [[ "$default_yes" == true ]]; then
        suffix="[Y/n]"
    else
        suffix="[y/N]"
    fi

    echo -ne "  ${COLOR_MAGENTA}[?]${COLOR_RESET} $message $suffix " >&2
    read -r response

    if [[ -z "$response" ]]; then
        [[ "$default_yes" == true ]] && return 0 || return 1
    fi

    [[ "$response" =~ ^[Yy] ]] && return 0 || return 1
}

read_input() {
    local prompt="$1"
    local default="$2"
    local provided_value="${3:-}"

    # User messages sent to stderr to split them from the function output
    if [[ -n "$provided_value" ]]; then
        write_info "$prompt (provided: $provided_value)"
        echo "$provided_value"
        return 0
    fi

    if [[ "$auto_confirm" == true ]]; then
        write_info "$prompt (using default: $default)"
        echo "$default"
        return 0
    fi

    echo -ne "  ${COLOR_MAGENTA}[?]${COLOR_RESET} $prompt " >&2
    if [[ -n "$default" ]]; then
        echo -ne "${COLOR_DARK_GRAY}[$default]${COLOR_RESET} " >&2
    fi

    read -r response
    if [[ -z "$response" ]]; then
        echo "$default"
    else
        echo "$response"
    fi
}

# Arguments

show_usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Options:
    --zephyr                    Use upstream Zephyr RTOS configuration
    --ncs                       Use Nordic nRF Connect SDK configuration
    --board BOARD               Board ID to configure (required)
    --workspace-folder DIR      Workspace folder path
    --spotflow-revision REV     Spotflow module revision to checkout
    --quickstart-json-url URL   Override quickstart.json URL
    --github-token TOKEN       Optional GitHub token for Zephyr SDK installation
    --auto-confirm              Automatically confirm all actions
    -h, --help                  Show this help message

Examples:
    $0 --zephyr --board frdm_rw612
    $0 --ncs --board nrf7002dk
    $0 --zephyr --board frdm_rw612 --workspace-folder ~/my-workspace --auto-confirm
    $0 --zephyr --board frdm_rw612 --quickstart-json-url http://localhost:8080/quickstart.json
EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --zephyr)
                if [[ -n "$sdk_type" ]]; then
                    exit_with_error "Cannot specify both --zephyr and --ncs"
                fi
                sdk_type="zephyr"
                shift
                ;;
            --ncs)
                if [[ -n "$sdk_type" ]]; then
                    exit_with_error "Cannot specify both --zephyr and --ncs"
                fi
                sdk_type="ncs"
                shift
                ;;
            --board)
                board="$2"
                shift 2
                ;;
            --workspace-folder)
                workspace_folder="$2"
                shift 2
                ;;
            --spotflow-revision)
                spotflow_revision="$2"
                shift 2
                ;;
            --quickstart-json-url)
                quickstart_json_url="$2"
                shift 2
                ;;
            --github-token)
                github_token="$2"
                shift 2
                ;;
            --auto-confirm)
                auto_confirm=true
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                echo "Unknown option: $1" >&2
                show_usage >&2
                exit 1
                ;;
        esac
    done

    # Validate required arguments
    if [[ -z "$sdk_type" ]]; then
        exit_with_error "Must specify either --zephyr or --ncs" \
            "Use --zephyr for upstream Zephyr or --ncs for nRF Connect SDK."
    fi

    if [[ -z "$board" ]]; then
        exit_with_error "Must specify --board" \
            "Use --board to specify the board ID (e.g., frdm_rw612)."
    fi

    if [[ "$sdk_type" == "zephyr" ]]; then
        sdk_display_type="Zephyr"
    else
        sdk_display_type="nRF Connect SDK"
    fi
}

# Prerequisites

find_python() {
    local python_cmd=""
    for cmd in python3 python; do
        if command -v "$cmd" &>/dev/null; then
            local version
            version=$("$cmd" --version 2>&1)
            python_cmd="$cmd"
            write_success "Python found: $version"
            break
        fi
    done

    if [[ -z "$python_cmd" ]]; then
        exit_with_error "Python not found" \
            "Please install Python 3.10+ or follow Zephyr's Getting Started Guide."
    fi

    echo "$python_cmd"
}

check_git() {
    if ! command -v git &>/dev/null; then
        exit_with_error "Git not found" \
            "Please install Git using your package manager."
    fi
    local git_version
    git_version=$(git --version)
    write_success "Git found: $git_version"
}

# Board configuration

# Extracts board configuration from quickstart.json to a simple format
# Returns:
#   If not found:
#     available_boards=esp32_devkitc, esp32_ethernet_kit, ...
#   If found:
#     vendor_name=NXP   (only if sdk_type is "zephyr")
#     id=frdm_rw612
#     name=FRDM-RW612
#     board=frdm_rw612
#     connection=ethernet, wifi
#     ... (remaining fields)
extract_board_config() {
    local python_cmd="$1"
    local json="$2"
    local sdk_type="$3"
    local board_id="$4"

    # Use Python to avoid introducing new dependencies like jq
    "$python_cmd" -c '
import sys
import json

try:
    data = json.load(sys.stdin)

    sdk_type = sys.argv[1]
    board_id = sys.argv[2]

    if sdk_type == "ncs":
        vendors = [data.get("ncs", {})]
    else:
        vendors = data.get("zephyr", {}).get("vendors", [])

    available_boards = []
    for vendor in vendors:
        vendor_name = vendor.get("name", "")
        boards = vendor.get("boards", [])
        for board in boards:
            available_boards.append(board["id"])

            if board["id"] == board_id:
                print("vendor_name=%s" % vendor_name)

                board["connection"] = ", ".join(board.get("connection", []))
                for key, value in board.items():
                    print("%s=%s" % (key, value))

                sys.exit(0)

    print("available_boards=%s" % ", ".join(available_boards))

except Exception as e:
    sys.stderr.write(f"JSON parsing error: {e}\n")
    sys.exit(1)
' "$sdk_type" "$board_id" <<< "$json"
}

# Extracts a parameter from the board configuration string
# Usage: extract_board_parameter <board_config> <parameter>
# Returns:
#   The value of the parameter or empty string if not found
extract_board_parameter() {
    local board_config="$1"
    local parameter="$2"

    echo "$board_config" | grep "^$parameter=" | cut -d'=' -f2- || true
}

download_quickstart_json() {
    local json_url="${quickstart_json_url:-$DEFAULT_QUICKSTART_JSON_URL}"
    if ! quickstart_json=$(curl -fsSL "$json_url"); then
        exit_with_error "Failed to download board configuration" \
            "Could not fetch $json_url"
    fi
    write_success "Configuration downloaded successfully"

    echo "$quickstart_json"
}

lookup_board_config() {
    local python_cmd="$1"
    local json="$2"

    local board_config
    if ! board_config=$(extract_board_config "$python_cmd" "$json" "$sdk_type" "$board"); then
        exit_with_error "Failed to parse board configuration" \
            "JSON parsing failed"
    fi

    local available_boards
    available_boards=$(extract_board_parameter "$board_config" "available_boards")

    if [[ -n "$available_boards" ]]; then
        exit_with_error \
            "Board '$board' not found in $sdk_display_type configuration" \
            "Available boards: $available_boards"
    fi

    echo "$board_config"
}

write_callout() {
    local callout="$1"

    if [[ -n "$callout" ]]; then
        local callout_text
        callout_text=$(echo "$callout" | sed 's/<[^>]*>//g')
        echo ""
        write_warning "Note: $callout_text"
    fi
}

# Workspace folder

# Convert a path to absolute path without requiring it to exist
# Works on both Linux and macOS
to_absolute_path() {
    local path="$1"

    # Handle empty path
    if [[ -z "$path" ]]; then
        echo "$PWD"
        return
    fi

    # Expand tilde to home directory
    if [[ "$path" =~ ^~(/|$) ]]; then
        path="${HOME}${path:1}"
    fi

    # If already absolute, use as-is
    if [[ "$path" = /* ]]; then
        echo "$path"
        return
    fi

    # Make relative path absolute
    local absolute="$PWD/$path"

    echo "$absolute"
}

get_workspace_folder() {
    local default_folder="$HOME/spotflow-ws"

    write_info \
        "The workspace will contain all $sdk_display_type files and project."
    echo "" >&2

    workspace_folder=$(read_input "Enter workspace folder path" \
        "$default_folder" "$workspace_folder")

    if [[ -z "$workspace_folder" ]]; then
        exit_with_error "Workspace folder path cannot be empty"
    fi

    workspace_folder=$(to_absolute_path "$workspace_folder")

    if [[ -d "$workspace_folder" ]] && [[ -n "$(ls -A "$workspace_folder" 2>/dev/null)" ]]; then
        write_warning "Folder '$workspace_folder' already exists and is not empty."
        if ! confirm_action "Continue anyway? This may cause issues." false; then
            write_info "Setup cancelled by user."
            exit 0
        fi
    fi

    write_success "Workspace folder: $workspace_folder"

    echo "$workspace_folder"
}

create_workspace_folder() {
    if [[ ! -d "$workspace_folder" ]]; then
        if ! mkdir -p "$workspace_folder"; then
            exit_with_error "Failed to create workspace folder" "$workspace_folder"
        fi
        write_success "Created folder: $workspace_folder"
    else
        write_info "Folder already exists: $workspace_folder"
    fi
}

# Workspace initialization

create_python_venv() {
    local venv_path="$workspace_folder/.venv"

    if [[ -d "$venv_path" ]]; then
        write_info "Virtual environment already exists, reusing it"
    else
        if ! "$python_cmd" -m venv .venv; then
            exit_with_error "Failed to create Python virtual environment"
        fi
        write_success "Virtual environment created"
    fi

    echo "$venv_path"
}

activate_python_venv() {
    local venv_path="$1"

    local activate_script="$venv_path/bin/activate"
    if [[ ! -f "$activate_script" ]]; then
        exit_with_error "Virtual environment activation script not found" "$activate_script"
    fi

    # shellcheck disable=SC1090
    source "$activate_script"
    write_success "Virtual environment activated"
}

install_west() {
    if ! pip install west; then
        exit_with_error "Failed to install West"
    fi
    write_success "West installed successfully"
}

initialize_west_workspace() {
    local manifest="$1"
    local manifest_file="zephyr/manifests/$manifest"

    write_info "Manifest URL: $MANIFEST_BASE_URL"
    write_info "Manifest file: $manifest_file"

    local west_init_args=(init --manifest-url "$MANIFEST_BASE_URL" \
        --manifest-file "$manifest_file" .)
    if [[ -n "$spotflow_revision" ]]; then
        west_init_args+=(--clone-opt="--revision=$spotflow_revision")
    fi

    if west_init_output=$(west "${west_init_args[@]}" 2>&1); then
        write_success "West workspace initialized"
    else
        if echo "$west_init_output" | grep -q "already initialized"; then
            write_warning "Workspace already initialized, continuing..."
        else
            exit_with_error "Failed to initialize West workspace" "$west_init_output"
        fi
    fi
}

update_west_workspace() {
    if ! west update --fetch-opt=--depth=1 --narrow; then
        exit_with_error "Failed to download dependencies"
    fi
    write_success "Dependencies downloaded"
}

install_python_packages() {
    if ! west packages pip --install; then
        exit_with_error "Failed to install Python packages"
    fi
    write_success "Python packages installed"
}

fetch_blobs() {
    local blob="$1"

    if ! west blobs fetch "$blob" --auto-accept; then
        exit_with_error "Failed to fetch binary blobs"
    fi
    write_success "Binary blobs fetched"
}

# Zephyr SDK

check_zephyr_sdk_installation() {
    local sdk_version="$1"
    local sdk_toolchain="$2"

    local sdk_installed=false
    local toolchain_installed=false
    local install_sdk=false

    local sdk_locations=(
        "$HOME/zephyr-sdk-$sdk_version"
        "$HOME/.local/zephyr-sdk-$sdk_version"
        "/opt/zephyr-sdk-$sdk_version"
        "/usr/local/zephyr-sdk-$sdk_version"
    )

    if [[ -n "${ZEPHYR_SDK_INSTALL_DIR:-}" ]]; then
        sdk_locations=(
            "$ZEPHYR_SDK_INSTALL_DIR"
            "$ZEPHYR_SDK_INSTALL_DIR/zephyr-sdk-$sdk_version"
            "${sdk_locations[@]}"
        )
    fi

    for sdk_path in "${sdk_locations[@]}"; do
        if [[ -f "$sdk_path/sdk_version" ]]; then
            write_success "Found Zephyr SDK at: $sdk_path"
            sdk_installed=true

            if [[ -n "$sdk_toolchain" ]]; then
                if [[ -d "$sdk_path/$sdk_toolchain" ]]; then
                    write_success "Toolchain '$sdk_toolchain' is installed"
                    toolchain_installed=true
                else
                    write_warning "Toolchain '$sdk_toolchain' not found in SDK"
                fi
            else
                toolchain_installed=true
            fi
            break
        fi
    done

    if [[ "$sdk_installed" == false ]] || [[ "$toolchain_installed" == false ]]; then
        local warning_msg="Zephyr SDK $sdk_version"
        if [[ -n "$sdk_toolchain" ]]; then
            warning_msg+=" with toolchain '$sdk_toolchain'"
        fi
        warning_msg+=" is not installed."
        write_warning "$warning_msg"
        echo "" >&2
        write_info "Zephyr SDK will be installed to your user profile directory."
        echo "" >&2

        local confirm_msg="Install Zephyr SDK $sdk_version"
        if [[ -n "$sdk_toolchain" ]]; then
            confirm_msg+=" with '$sdk_toolchain' toolchain"
        fi
        confirm_msg+="?"

        if confirm_action "$confirm_msg" true; then
            install_sdk=true
        else
            write_warning "Skipping SDK installation. You can install it manually later with:"
            local manual_cmd="west sdk install --version $sdk_version"
            if [[ -n "$sdk_toolchain" ]]; then
                manual_cmd+=" --toolchains $sdk_toolchain"
            fi
            echo -e "    ${COLOR_DARK_GRAY}$manual_cmd${COLOR_RESET}" >&2
        fi
    else
        write_success "Zephyr SDK is properly installed"
    fi

    echo $install_sdk
}

install_zephyr_sdk() {
    local sdk_version="$1"
    local sdk_toolchain="$2"
    local github_token="$3"

    local sdk_args=(sdk install --version "$sdk_version")
    if [[ -n "$sdk_toolchain" ]]; then
        sdk_args+=(--toolchains "$sdk_toolchain")
    fi
    if [[ -n "$github_token" ]]; then
        sdk_args+=(--personal-access-token "$github_token")
    fi

    write_info "Running: west ${sdk_args[*]}"

    if west "${sdk_args[@]}"; then
        write_success "Zephyr SDK installed"
    else
        write_error "Failed to install Zephyr SDK"
        write_warning "You can try installing it manually later with: west ${sdk_args[*]}"
    fi
}

# nRF Connect SDK

test_nrfutil_has_sdk_manager() {
    local nrfutil_path="$1"
    "$nrfutil_path" sdk-manager --version &>/dev/null
}

find_nrfutil_sdk_manager_in_extensions() {
    local extension_dirs=(
        "$HOME/.vscode/extensions"
        "$HOME/.cursor/extensions"
        "$HOME/.vscode-insiders/extensions"
    )
    local sdk_manager_rel_path="platform/nrfutil/bin/nrfutil-sdk-manager"

    for ext_dir in "${extension_dirs[@]}"; do
        if [[ ! -d "$ext_dir" ]]; then
            continue
        fi

        # Find Nordic Semiconductor extensions
        while IFS= read -r -d '' ext_path; do
            local sdk_manager_path="$ext_path/$sdk_manager_rel_path"
            if [[ -x "$sdk_manager_path" ]]; then
                echo "$sdk_manager_path"
                return 0
            fi
        done < <(find "$ext_dir" -maxdepth 1 -type d \
            -name "nordic-semiconductor.nrf-connect*" -print0 2>/dev/null)
    done

    return 1
}

find_nrfutil() {
    # Returns: type|path|needs_sdk_manager
    # type: nrfutil or sdk-manager
    # needs_sdk_manager: true or false

    local nrfutil_on_path=""
    local nrfutil_has_sdk_manager=false

    if command -v nrfutil &>/dev/null; then
        nrfutil_on_path="$(command -v nrfutil)"
        if test_nrfutil_has_sdk_manager "$nrfutil_on_path"; then
            nrfutil_has_sdk_manager=true
        fi
    fi

    if [[ -n "$nrfutil_on_path" ]] && \
        [[ "$nrfutil_has_sdk_manager" == true ]]; then
        echo "nrfutil|$nrfutil_on_path|false"
        return 0
    fi

    local extension_sdk_manager
    if extension_sdk_manager=$(find_nrfutil_sdk_manager_in_extensions); then
        echo "sdk-manager|$extension_sdk_manager|false"
        return 0
    fi

    if [[ -n "$nrfutil_on_path" ]]; then
        echo "nrfutil|$nrfutil_on_path|true"
        return 0
    fi

    return 1
}

install_nrfutil_sdk_manager() {
    local nrfutil_path="$1"

    write_info "Installing nrfutil sdk-manager command..."
    if "$nrfutil_path" install sdk-manager &>/dev/null; then
        write_success "nrfutil sdk-manager installed"
        return 0
    else
        write_error "Failed to install nrfutil sdk-manager"
        return 1
    fi
}

test_ncs_toolchain_installed() {
    local nrfutil_path="$1"
    local nrfutil_type="$2"
    local ncs_version="$3"

    local output
    if [[ "$nrfutil_type" == "nrfutil" ]]; then
        output=$("$nrfutil_path" sdk-manager toolchain list 2>&1) || return 1
    else
        output=$("$nrfutil_path" toolchain list 2>&1) || return 1
    fi

    if echo "$output" | grep -qF "$ncs_version"; then
        return 0
    fi

    return 1
}

check_ncs_installation() {
    local nrfutil_info=""
    local nrfutil_path=""
    local nrfutil_type=""
    local needs_sdk_manager_install=false

    if nrfutil_info=$(find_nrfutil); then
        IFS='|' read -r nrfutil_type nrfutil_path needs_sdk_manager_install <<< "$nrfutil_info"

        if [[ "$nrfutil_type" == "nrfutil" ]]; then
            if [[ "$needs_sdk_manager_install" == "true" ]]; then
                write_info "Found nrfutil at: $nrfutil_path"
                write_warning \
                    "The sdk-manager command is not installed for nrfutil."
                echo "" >&2
                write_info \
                    "This command is required to install the nRF Connect SDK toolchain."
                echo "" >&2

                if confirm_action "Install nrfutil sdk-manager command?" true; then
                    if install_nrfutil_sdk_manager "$nrfutil_path"; then
                        needs_sdk_manager_install=false
                    else
                        write_warning \
                            "Could not install nrfutil sdk-manager command."
                        write_warning \
                            "Toolchain installation will be skipped. Install it manually later."
                        nrfutil_info=""
                    fi
                else
                    local msg="Skipping sdk-manager installation."
                    msg="${msg} Toolchain will need to be installed manually."
                    write_warning $msg
                    nrfutil_info=""
                fi
            else
                write_success \
                    "Found nrfutil with sdk-manager at: $nrfutil_path"
            fi
        else
            write_success "Found nrfutil-sdk-manager at: $nrfutil_path"
        fi
    else
        local msg="nrfutil not found on PATH"
        msg="${msg} and nrfutil-sdk-manager not found in VS Code/Cursor extensions."
        write_warning $msg
        echo "" >&2
        write_info "The nRF Connect SDK toolchain will need to be installed manually."
        local nrfutil_url="https://www.nordicsemi.com/Products/Development-tools/nRF-Util"
        write_info "You can install nrfutil from: $nrfutil_url"
        write_info "Or install the nRF Connect for VS Code extension pack."
        echo "" >&2
    fi

    # Check if nRF Connect SDK toolchain is installed and prompt for installation
    if [[ -n "$nrfutil_info" ]] && [[ -n "$ncs_version" ]]; then
        if test_ncs_toolchain_installed "$nrfutil_path" "$nrfutil_type" "$ncs_version"; then
            write_success "nRF Connect SDK toolchain for $ncs_version is already installed"
        else
            write_warning "nRF Connect SDK toolchain for $ncs_version is not installed."
            echo "" >&2
            write_info "The installation may take several minutes."
            echo "" >&2

            if confirm_action "Install nRF Connect SDK toolchain for $ncs_version?" true; then
                should_install_ncs_toolchain=true
            else
                local msg="Skipping toolchain installation."
                msg="${msg} You can install it manually later with:"
                $command="nrfutil sdk-manager toolchain install --ncs-version $ncs_version"
                write_warning $msg
                echo -e "    ${COLOR_DARK_GRAY}$command${COLOR_RESET}" >&2
            fi
        fi
    elif [[ -z "$ncs_version" ]]; then
        write_warning "nRF Connect SDK version not specified in board configuration."
        write_info "Toolchain installation will need to be done manually."
    fi

    if [[ "$should_install_ncs_toolchain" == true ]]; then
        if [[ $nrfutil_type == "nrfutil" ]]; then
            echo "$nrfutil_path sdk-manager"
        else
            echo "$nrfutil_path"
        fi
    fi
}

install_ncs_toolchain() {
    local sdk_manager_cmd="$1"
    local ncs_version="$2"

    local manager_args="toolchain install --ncs-version $ncs_version"

    write_info "Running: $sdk_manager_cmd $manager_args"

    if "$sdk_manager_cmd" $manager_args; then
        write_success "nRF Connect SDK toolchain installed for version $ncs_version"
    else
        write_error "Failed to install nRF Connect SDK toolchain"
        write_warning "You can try installing it manually with: $sdk_manager_cmd $manager_args"
    fi
}

# Configuration

get_quickstart_url() {
    local quickstart_url_suffix
    if [[ "$sdk_type" == "zephyr" ]]; then
        quickstart_url_suffix="zephyr"
    else
        quickstart_url_suffix="nordic-nrf-connect"
    fi
    echo "https://docs.spotflow.io/quickstart/$quickstart_url_suffix"
}

add_configuration_placeholders() {
    local config_path="$1"
    local quickstart_url="$2"

    local config_content
    config_content=$(cat "$config_path")

    if echo "$config_content" | grep -q "CONFIG_SPOTFLOW_DEVICE_ID"; then
        write_info "CONFIG_SPOTFLOW_DEVICE_ID already exists in prj.conf, skipping"
        return 0
    fi

    local prepended_config_content=""

    if echo "$connection_methods" | grep -q "wifi"; then
        prepended_config_content+="CONFIG_NET_WIFI_SSID=\"<Your Wi-Fi SSID>\"\n"
        prepended_config_content+="CONFIG_NET_WIFI_PASSWORD=\"<Your Wi-Fi Password>\"\n\n"
    fi

    local sample_device_id
    sample_device_id=$(extract_board_parameter "$board_config" "sample_device_id")
    prepended_config_content+="CONFIG_SPOTFLOW_DEVICE_ID=\"$sample_device_id\"\n"
    prepended_config_content+="CONFIG_SPOTFLOW_INGEST_KEY=\"<Your Spotflow Ingest Key>\"\n\n"

    config_content="${prepended_config_content}${config_content}"

    if echo -e "$config_content" > "$config_path"; then
        write_success "Configuration placeholders added to prj.conf"
    else
        write_error "Failed to add configuration placeholders to prj.conf"
        write_warning \
            "See the quickstart guide for the list of configuration options: $quickstart_url"
    fi
}

main() {
    echo ""
    echo -e "${COLOR_CYAN}+--------------------------------------------------------+${COLOR_RESET}"
    echo -e "${COLOR_CYAN}|         Spotflow Device SDK - Workspace Setup          |${COLOR_RESET}"
    echo -e "${COLOR_CYAN}+--------------------------------------------------------+${COLOR_RESET}"
    echo ""

    parse_args "$@"

    write_info "SDK type: $sdk_display_type"
    write_info "Board ID: $board"

    write_step "Checking prerequisites..."
    local python_cmd
    python_cmd=$(find_python)
    check_git

    write_step "Downloading board configuration..."
    local quickstart_json
    quickstart_json=$(download_quickstart_json)

    write_step "Looking up board '$board'..."
    local board_config
    board_config=$(lookup_board_config "$python_cmd" "$quickstart_json")

    # Extract board configuration values
    local board_name=$(extract_board_parameter "$board_config" "name")
    local board_target=$(extract_board_parameter "$board_config" "board")
    local manifest=$(extract_board_parameter "$board_config" "manifest")
    local sdk_version=$(extract_board_parameter "$board_config" "sdk_version")
    local connection_methods=$(extract_board_parameter "$board_config" "connection")
    local sdk_toolchain=$(extract_board_parameter "$board_config" "sdk_toolchain")
    local blob=$(extract_board_parameter "$board_config" "blob")
    local ncs_version=$(extract_board_parameter "$board_config" "ncs_version")
    local callout=$(extract_board_parameter "$board_config" "callout")
    local spotflow_path=$(extract_board_parameter "$board_config" "spotflow_path")
    local build_extra_args=$(extract_board_parameter "$board_config" "build_extra_args")

    if [[ "$sdk_type" == "zephyr" ]]; then
        vendor_name=$(extract_board_parameter "$board_config" "vendor_name")
    fi

    write_success "Found board: $board_name"
    if [[ -n "$vendor_name" ]]; then
        write_info "Vendor: $vendor_name"
    fi
    write_info "Connection methods: $connection_methods"
    write_info "Board target: $board_target"
    write_info "Manifest: $manifest"
    write_info "SDK version: $sdk_version"
    if [[ -n "$sdk_toolchain" ]]; then
        write_info "Toolchain: $sdk_toolchain"
    fi
    if [[ -n "$blob" ]]; then
        write_info "Blob: $blob"
    fi
    if [[ -n "$ncs_version" ]]; then
        write_info "nRF Connect SDK version: $ncs_version"
    fi

    write_callout "$callout"

    write_step "Configuring workspace location..."
    workspace_folder=$(get_workspace_folder)

    local should_install_zephyr_sdk=false
    local should_install_ncs_toolchain=false
    local ncs_manager_cmd=""

    if [[ "$sdk_type" == "ncs" ]]; then
        write_step "Checking nRF Connect SDK toolchain setup..."
        ncs_manager_cmd=$(check_ncs_installation)
        if [[ -n "$ncs_manager_cmd" ]]; then
            should_install_ncs_toolchain=true
        fi
    else
        write_step "Checking Zephyr SDK installation..."
        should_install_zephyr_sdk=$(check_zephyr_sdk_installation "$sdk_version" "$sdk_toolchain")
    fi

    write_step "Creating workspace folder..."
    create_workspace_folder

    # Change to workspace folder
    if ! cd "$workspace_folder"; then
        exit_with_error "Failed to change to workspace folder" "$workspace_folder"
    fi
    write_success "Changed to workspace folder"

    write_step "Creating Python virtual environment..."
    local venv_path
    venv_path=$(create_python_venv)

    write_step "Activating virtual environment..."
    activate_python_venv "$venv_path"

    write_step "Installing West..."
    install_west

    write_step "Initializing West workspace..."
    initialize_west_workspace "$manifest"

    write_step "Downloading dependencies (this may take several minutes)..."
    update_west_workspace

    write_step "Installing Python packages..."
    install_python_packages

    if [[ -n "$blob" ]]; then
        write_step "Fetching binary blobs for $blob..."
        fetch_blobs "$blob"
    fi

    if [[ "$should_install_ncs_toolchain" == true ]]; then
        write_step "Installing nRF Connect SDK toolchain for $ncs_version..."
        install_ncs_toolchain "$ncs_manager_cmd" "$ncs_version"
    elif [[ "$should_install_zephyr_sdk" == true ]]; then
        write_step "Installing Zephyr SDK $sdk_version..."
        install_zephyr_sdk "$sdk_version" "$sdk_toolchain" "$github_token"
    fi

    local quickstart_url
    quickstart_url=$(get_quickstart_url)

    local sample_path="$spotflow_path/zephyr/samples/logs"
    local config_path="$sample_path/prj.conf"

    write_step "Adding configuration placeholders to prj.conf..."
    add_configuration_placeholders "$config_path" "$quickstart_url"

    # Summary
    local build_cmd="west build --pristine --board $board_target"
    if [[ -n "$build_extra_args" ]]; then
        build_cmd+=" $build_extra_args"
    fi
    echo ""
    echo -e "${COLOR_GREEN}+--------------------------------------------------------+${COLOR_RESET}"
    echo -e "${COLOR_GREEN}|                     Setup Complete!                    |${COLOR_RESET}"
    echo -e "${COLOR_GREEN}+--------------------------------------------------------+${COLOR_RESET}"
    echo ""
    echo -n "Workspace location: "
    echo -e "${COLOR_CYAN}$workspace_folder${COLOR_RESET}"
    echo -n "Board: "
    echo -e "${COLOR_CYAN}$board_name ($board_target)${COLOR_RESET}"
    echo -n "Spotflow module path: "
    echo -e "${COLOR_CYAN}$spotflow_path${COLOR_RESET}"
    echo ""
    echo -e "${COLOR_YELLOW}Next steps to finish the quickstart:${COLOR_RESET}"
    echo ""
    echo -n "  1. Open "
    echo -ne "${COLOR_DARK_GRAY}$config_path${COLOR_RESET}"
    echo " and fill in the required configuration options."
    echo ""
    if [[ "$sdk_type" == "zephyr" ]]; then
        echo "  2. Build and flash the Spotflow sample:"
    else
        echo -n "  2. In a terminal with the nRF Connect toolchain environment, "
        echo "build and flash the Spotflow sample:"
    fi
    echo ""
    echo -e "     ${COLOR_DARK_GRAY}cd $sample_path${COLOR_RESET}"
    echo -e "     ${COLOR_DARK_GRAY}$build_cmd${COLOR_RESET}"
    echo -e "     ${COLOR_DARK_GRAY}west flash${COLOR_RESET}"
    echo ""
    echo -n "For more information, visit: "
    echo -e "${COLOR_CYAN}$quickstart_url${COLOR_RESET}"
    echo ""
}

main "$@"
