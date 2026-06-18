#!/usr/bin/env sh
set -eu

TEST_PATH="${1:-tests/ble_framing}"
PLATFORM="${2:-native_sim}"

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODULE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
VENV_PYTHON="$MODULE_DIR/../../../../.venv/bin/python"
TWISTER="$MODULE_DIR/../../../../zephyr/scripts/twister"
RESOLVED_TEST_PATH="$MODULE_DIR/$TEST_PATH"

if [ ! -f "$TWISTER" ]; then
    echo "Twister script not found: $TWISTER" >&2
    exit 1
fi

if [ ! -f "$VENV_PYTHON" ]; then
    echo "Virtual environment python not found: $VENV_PYTHON" >&2
    exit 1
fi

if [ ! -d "$RESOLVED_TEST_PATH" ]; then
    echo "Test suite path not found: $RESOLVED_TEST_PATH" >&2
    exit 1
fi

cd "$MODULE_DIR"
"$VENV_PYTHON" "$TWISTER" --platform "$PLATFORM" --testsuite-root "$RESOLVED_TEST_PATH"
