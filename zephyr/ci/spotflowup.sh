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
auto_confirm=false
quickstart_json_url=""

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

# Formatting functions
write_step() {
    echo -e "\n${COLOR_CYAN}>${COLOR_WHITE} $1${COLOR_RESET}"
}

write_info() {
    echo -e "  ${COLOR_BLUE}[i]${COLOR_GRAY} $1${COLOR_RESET}"
}

write_success() {
    echo -e "  ${COLOR_GREEN}[+]${COLOR_WHITE} $1${COLOR_RESET}"
}

write_warning() {
    echo -e "  ${COLOR_YELLOW}[!] $1${COLOR_RESET}"
}

write_error() {
    echo -e "  ${COLOR_RED}[x] $1${COLOR_RESET}"
}

exit_with_error() {
    local message="$1"
    local details="${2:-}"
    local discord_url="https://discord.com/channels/1372202003635114125/1379411086163574864"

    echo ""
    write_error "$message"
    if [[ -n "$details" ]]; then
        echo -e "  ${COLOR_DARK_GRAY}Details: $details${COLOR_RESET}"
    fi
    echo ""
    echo -e "${COLOR_GRAY}If you believe this is a bug, please report it at:${COLOR_RESET}"
    echo -e "  ${COLOR_CYAN}https://github.com/spotflow-io/device-sdk/issues${COLOR_RESET}"
    echo ""
    echo -e "${COLOR_GRAY}You can also contact us on Discord:${COLOR_RESET}"
    echo -e "  ${COLOR_CYAN}${discord_url}${COLOR_RESET}"
    echo ""
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

    echo -ne "  ${COLOR_MAGENTA}[?]${COLOR_RESET} $message $suffix "
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
        write_info "$prompt (provided: $provided_value)" >&2
        echo "$provided_value"
        return 0
    fi

    if [[ "$auto_confirm" == true ]]; then
        write_info "$prompt (using default: $default)" >&2
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
}

main() {
    echo ""
    echo -e "${COLOR_CYAN}+--------------------------------------------------------+${COLOR_RESET}"
    echo -e "${COLOR_CYAN}|         Spotflow Device SDK - Workspace Setup          |${COLOR_RESET}"
    echo -e "${COLOR_CYAN}+--------------------------------------------------------+${COLOR_RESET}"
    echo ""

    parse_args "$@"

    local sdk_display_type
    if [[ "$sdk_type" == "zephyr" ]]; then
        sdk_display_type="Zephyr"
    else
        sdk_display_type="nRF Connect SDK"
    fi

    write_info "SDK type: $sdk_display_type"
    write_info "Board ID: $board"

    # Step 1: Download and parse quickstart.json
    write_step "Downloading board configuration..."

    # Use provided URL or default
    local json_url="${quickstart_json_url:-$DEFAULT_QUICKSTART_JSON_URL}"

    local quickstart_json
    if ! quickstart_json=$(curl -fsSL "$json_url"); then
        exit_with_error "Failed to download board configuration" \
            "Could not fetch $json_url"
    fi
    write_success "Configuration downloaded successfully"

    # Step 2: Find the board in the configuration
    write_step "Looking up board '$board'..."

    local board_config
    local vendor_name=""
    local jq_query

    if [[ "$sdk_type" == "ncs" ]]; then
        jq_query=".ncs.boards[] | select(.id == \"$board\")"
    else
        jq_query=".zephyr.vendors[] | .boards[] | select(.id == \"$board\")"
    fi

    if ! board_config=$(echo "$quickstart_json" | jq -c "$jq_query" | \
        head -n1); then
        exit_with_error "Failed to parse board configuration" \
            "jq parsing failed"
    fi

    if [[ -z "$board_config" ]] || [[ "$board_config" == "null" ]]; then
        local available_boards
        if [[ "$sdk_type" == "ncs" ]]; then
            available_boards=$(echo "$quickstart_json" | \
                jq -r '.ncs.boards[]?.id' | tr '\n' ', ')
        else
            available_boards=$(echo "$quickstart_json" | \
                jq -r '.zephyr.vendors[]?.boards[]?.id' | tr '\n' ', ')
        fi
        exit_with_error \
            "Board '$board' not found in $sdk_display_type configuration" \
            "Available boards: ${available_boards%, }"
    fi

    # Extract board configuration values
    local board_name board_target manifest sdk_version sdk_toolchain
    local blob ncs_version callout spotflow_path build_extra_args
    board_name=$(echo "$board_config" | jq -r '.name')
    board_target=$(echo "$board_config" | jq -r '.board')
    manifest=$(echo "$board_config" | jq -r '.manifest')
    sdk_version=$(echo "$board_config" | jq -r '.sdk_version')
    sdk_toolchain=$(echo "$board_config" | jq -r '.sdk_toolchain // empty')
    blob=$(echo "$board_config" | jq -r '.blob // empty')
    ncs_version=$(echo "$board_config" | jq -r '.ncs_version // empty')
    callout=$(echo "$board_config" | jq -r '.callout // empty')
    spotflow_path=$(echo "$board_config" | jq -r '.spotflow_path')
    build_extra_args=$(echo "$board_config" | \
        jq -r '.build_extra_args // empty')

    if [[ "$sdk_type" == "zephyr" ]]; then
        vendor_name=$(echo "$quickstart_json" | \
            jq -r ".zephyr.vendors[] | select(.boards[].id == \"$board\") | .name" | head -n1)
    fi

    write_success "Found board: $board_name"
    if [[ -n "$vendor_name" ]]; then
        write_info "Vendor: $vendor_name"
    fi
    local connection_methods
    connection_methods=$(echo "$board_config" | jq -r '.connection[]' | tr '\n' ', ' | sed 's/,$//')
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

    # Show callout if present
    if [[ -n "$callout" ]]; then
        local callout_text
        callout_text=$(echo "$callout" | sed 's/<[^>]*>//g')
        echo ""
        write_warning "Note: $callout_text"
    fi

    # Step 3: Determine and confirm workspace folder
    write_step "Configuring workspace location..."

    local default_folder="$HOME/spotflow-ws"

    write_info \
        "The workspace will contain all $sdk_display_type files and project."
    echo ""

    workspace_folder=$(read_input "Enter workspace folder path" \
        "$default_folder" "$workspace_folder")

    if [[ -z "$workspace_folder" ]]; then
        exit_with_error "Workspace folder path cannot be empty"
    fi

    # Resolve to absolute path
    workspace_folder=$(to_absolute_path "$workspace_folder")

    if [[ -d "$workspace_folder" ]] && [[ -n "$(ls -A "$workspace_folder" 2>/dev/null)" ]]; then
        write_warning "Folder '$workspace_folder' already exists and is not empty."
        if ! confirm_action "Continue anyway? This may cause issues." false; then
            write_info "Setup cancelled by user."
            exit 0
        fi
    fi

    write_success "Workspace folder: $workspace_folder"

    # Step 4: Check prerequisites
    write_step "Checking prerequisites..."

    # Check Python
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

    # Check Git
    if ! command -v git &>/dev/null; then
        exit_with_error "Git not found" \
            "Please install Git using your package manager."
    fi
    local git_version
    git_version=$(git --version)
    write_success "Git found: $git_version"

    # Check jq (required for JSON parsing)
    if ! command -v jq &>/dev/null; then
        exit_with_error "jq not found" \
            "Please install jq using your package manager (e.g., apt install jq, brew install jq)."
    fi

    # Step 5: Check SDK installation
    local install_sdk=false
    local install_ncs_toolchain=false
    local nrfutil_info=""
    local nrfutil_path=""
    local nrfutil_type=""
    local needs_sdk_manager_install=false

    if [[ "$sdk_type" == "ncs" ]]; then
        write_step "Checking nRF Connect SDK toolchain setup..."

        if nrfutil_info=$(find_nrfutil); then
            IFS='|' read -r nrfutil_type nrfutil_path needs_sdk_manager_install <<< "$nrfutil_info"

            if [[ "$nrfutil_type" == "nrfutil" ]]; then
                if [[ "$needs_sdk_manager_install" == "true" ]]; then
                    write_info "Found nrfutil at: $nrfutil_path"
                    write_warning \
                        "The sdk-manager command is not installed for nrfutil."
                    echo ""
                    write_info \
                        "This command is required to install the nRF Connect SDK toolchain."
                    echo ""

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
                        local msg = "Skipping sdk-manager installation."
                        msg = "${msg} Toolchain will need to be installed manually."
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
            local msg = "nrfutil not found on PATH"
            msg = "${msg} and nrfutil-sdk-manager not found in VS Code/Cursor extensions."
            write_warning $msg
            echo ""
            write_info "The nRF Connect SDK toolchain will need to be installed manually."
            local nrfutil_url = "https://www.nordicsemi.com/Products/Development-tools/nRF-Util"
            write_info "You can install nrfutil from: $nrfutil_url"
            write_info "Or install the nRF Connect for VS Code extension pack."
            echo ""
        fi

        # Check if nRF Connect SDK toolchain is installed and prompt for installation
        if [[ -n "$nrfutil_info" ]] && [[ -n "$ncs_version" ]]; then
            if test_ncs_toolchain_installed "$nrfutil_path" "$nrfutil_type" "$ncs_version"; then
                write_success "nRF Connect SDK toolchain for $ncs_version is already installed"
            else
                write_warning "nRF Connect SDK toolchain for $ncs_version is not installed."
                echo ""
                write_info "The installation may take several minutes."
                echo ""

                if confirm_action "Install nRF Connect SDK toolchain for $ncs_version?" true; then
                    install_ncs_toolchain=true
                else
                    local msg = "Skipping toolchain installation."
                    msg = "${msg} You can install it manually later with:"
                    $command = "nrfutil sdk-manager toolchain install --ncs-version $ncs_version"
                    write_warning $msg
                    echo -e "    ${COLOR_DARK_GRAY}$command${COLOR_RESET}"
                fi
            fi
        elif [[ -z "$ncs_version" ]]; then
            write_warning "nRF Connect SDK version not specified in board configuration."
            write_info "Toolchain installation will need to be done manually."
        fi
    else
        write_step "Checking Zephyr SDK installation..."

        local sdk_installed=false
        local toolchain_installed=false

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
            echo ""
            write_info "Zephyr SDK will be installed to your user profile directory."
            echo ""

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
                echo -e "    ${COLOR_DARK_GRAY}$manual_cmd${COLOR_RESET}"
            fi
        else
            write_success "Zephyr SDK is properly installed"
        fi
    fi

    # Step 6: Create workspace folder
    write_step "Creating workspace folder..."

    if [[ ! -d "$workspace_folder" ]]; then
        if ! mkdir -p "$workspace_folder"; then
            exit_with_error "Failed to create workspace folder" "$workspace_folder"
        fi
        write_success "Created folder: $workspace_folder"
    else
        write_info "Folder already exists: $workspace_folder"
    fi

    # Change to workspace folder
    if ! cd "$workspace_folder"; then
        exit_with_error "Failed to change to workspace folder" "$workspace_folder"
    fi
    write_success "Changed to workspace folder"

    # Step 7: Create Python virtual environment
    write_step "Creating Python virtual environment..."

    local venv_path="$workspace_folder/.venv"

    if [[ -d "$venv_path" ]]; then
        write_info "Virtual environment already exists, reusing it"
    else
        if ! "$python_cmd" -m venv .venv; then
            exit_with_error "Failed to create Python virtual environment"
        fi
        write_success "Virtual environment created"
    fi

    # Activate virtual environment
    write_step "Activating virtual environment..."

    local activate_script="$venv_path/bin/activate"
    if [[ ! -f "$activate_script" ]]; then
        exit_with_error "Virtual environment activation script not found" "$activate_script"
    fi

    # shellcheck disable=SC1090
    source "$activate_script"
    write_success "Virtual environment activated"

    # Step 8: Install West
    write_step "Installing West..."

    if ! pip install west; then
        exit_with_error "Failed to install West"
    fi
    write_success "West installed successfully"

    # Step 9: Initialize West workspace
    write_step "Initializing West workspace..."

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

    # Step 10: Update West workspace
    write_step "Downloading dependencies (this may take several minutes)..."

    if ! west update --fetch-opt=--depth=1 --narrow; then
        exit_with_error "Failed to download dependencies"
    fi
    write_success "Dependencies downloaded"

    # Step 11: Install Python packages
    write_step "Installing Python packages..."

    if ! west packages pip --install; then
        exit_with_error "Failed to install Python packages"
    fi
    write_success "Python packages installed"

    # Step 12: Fetch blobs if required
    if [[ -n "$blob" ]]; then
        write_step "Fetching binary blobs for $blob..."

        if ! west blobs fetch "$blob" --auto-accept; then
            exit_with_error "Failed to fetch binary blobs"
        fi
        write_success "Binary blobs fetched"
    fi

    # Step 13: Install SDK/toolchain
    if [[ "$install_ncs_toolchain" == true ]] && [[ -n "$nrfutil_info" ]] && \
        [[ -n "$ncs_version" ]]; then
        write_step "Installing nRF Connect SDK toolchain for $ncs_version..."

        local toolchain_args
        local display_cmd
        if [[ "$nrfutil_type" == "nrfutil" ]]; then
            toolchain_args=(sdk-manager toolchain install --ncs-version "$ncs_version")
            display_cmd="nrfutil sdk-manager toolchain install --ncs-version $ncs_version"
        else
            toolchain_args=(toolchain install --ncs-version "$ncs_version")
            display_cmd="nrfutil-sdk-manager toolchain install --ncs-version $ncs_version"
        fi

        write_info "Running: $display_cmd"

        if "$nrfutil_path" "${toolchain_args[@]}"; then
            write_success "nRF Connect SDK toolchain installed for version $ncs_version"
        else
            write_error "Failed to install nRF Connect SDK toolchain"
            write_warning "You can try installing it manually with: $display_cmd"
        fi
    elif [[ "$install_sdk" == true ]]; then
        write_step "Installing Zephyr SDK $sdk_version..."

        local sdk_args=(sdk install --version "$sdk_version")
        if [[ -n "$sdk_toolchain" ]]; then
            sdk_args+=(--toolchains "$sdk_toolchain")
        fi

        write_info "Running: west ${sdk_args[*]}"

        if west "${sdk_args[@]}"; then
            write_success "Zephyr SDK installed"
        else
            write_error "Failed to install Zephyr SDK"
            local manual_cmd="west sdk install --version $sdk_version"
            if [[ -n "$sdk_toolchain" ]]; then
                manual_cmd+=" --toolchains $sdk_toolchain"
            fi
            write_warning "You can try installing it manually later with: $manual_cmd"
        fi
    fi

    local quickstart_url_suffix
    if [[ "$sdk_type" == "zephyr" ]]; then
        quickstart_url_suffix="zephyr"
    else
        quickstart_url_suffix="nordic-nrf-connect"
    fi
    local quickstart_url="https://docs.spotflow.io/quickstart/$quickstart_url_suffix"

    # Step 14: Add connection-specific configuration placeholders
    write_step "Adding configuration placeholders to prj.conf..."

    local sample_path="$spotflow_path/zephyr/samples/logs"
    local config_path="$sample_path/prj.conf"
    local config_content
    config_content=$(cat "$config_path")

    if echo "$config_content" | grep -q "CONFIG_SPOTFLOW_DEVICE_ID"; then
        write_warning "CONFIG_SPOTFLOW_DEVICE_ID already exists in prj.conf, skipping"
    else
        local prepended_config_content=""
        local connection_methods
        connection_methods=$(echo "$board_config" | jq -r '.connection[]?' 2>/dev/null)

        if echo "$connection_methods" | grep -q "wifi"; then
            prepended_config_content+="CONFIG_NET_WIFI_SSID=\"<Your Wi-Fi SSID>\"\n"
            prepended_config_content+="CONFIG_NET_WIFI_PASSWORD=\"<Your Wi-Fi Password>\"\n\n"
        fi

        local sample_device_id
        sample_device_id=$(echo "$board_config" | jq -r '.sample_device_id')
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
    fi

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
