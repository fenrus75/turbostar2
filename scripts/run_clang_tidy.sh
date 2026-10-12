#!/bin/bash
set -euo pipefail

# Locate repository root relative to this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Find run-clang-tidy binary
RUN_CLANG_TIDY=""
for candidate in run-clang-tidy run-clang-tidy-21 run-clang-tidy-20 run-clang-tidy-19; do
    if command -v "$candidate" >/dev/null 2>&1; then
        RUN_CLANG_TIDY="$candidate"
        break
    fi
done

if [ -z "$RUN_CLANG_TIDY" ]; then
    echo "Error: run-clang-tidy is not installed or not in PATH." >&2
    exit 1
fi

CONFIG_FILE="${REPO_ROOT}/.clang-tidy"
BUILD_DIR=""
FILES=()

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --critical)
            CONFIG_FILE="${REPO_ROOT}/.clang-tidy-critical"
            shift
            ;;
        --thorough)
            CONFIG_FILE="${REPO_ROOT}/.clang-tidy-thorough"
            shift
            ;;
        --config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        -p|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--critical] [--thorough] [--config CONFIG_FILE] [-p BUILD_DIR] [FILES...]"
            echo ""
            echo "Options:"
            echo "  --critical          Use .clang-tidy-critical configuration (high-priority safety subset)"
            echo "  --thorough          Use .clang-tidy-thorough configuration for deep-dive checks"
            echo "  --config FILE       Use specified clang-tidy configuration file"
            echo "  -p, --build-dir DIR Path to build directory containing compile_commands.json"
            echo "  FILES...            Optional file paths or regexes to check (default: all project files)"
            exit 0
            ;;
        *)
            if [ -z "$BUILD_DIR" ] && [ -d "$1" ] && [ -f "$1/compile_commands.json" ]; then
                BUILD_DIR="$1"
            else
                FILES+=("$1")
            fi
            shift
            ;;
    esac
done

# If build directory not specified, check default build dirs
if [ -z "$BUILD_DIR" ]; then
    if [ -n "${MESON_BUILD_ROOT:-}" ] && [ -f "${MESON_BUILD_ROOT}/compile_commands.json" ]; then
        BUILD_DIR="${MESON_BUILD_ROOT}"
    elif [ -f "${REPO_ROOT}/build/compile_commands.json" ]; then
        BUILD_DIR="${REPO_ROOT}/build"
    elif [ -f "${REPO_ROOT}/build-clang/compile_commands.json" ]; then
        BUILD_DIR="${REPO_ROOT}/build-clang"
    elif [ -f "compile_commands.json" ]; then
        BUILD_DIR="."
    else
        echo "Error: Could not locate compile_commands.json. Please specify build directory with -p <dir>." >&2
        exit 1
    fi
fi

if [ ! -f "${CONFIG_FILE}" ]; then
    echo "Error: Configuration file not found: ${CONFIG_FILE}" >&2
    exit 1
fi

echo "Running ${RUN_CLANG_TIDY} using config: $(basename "${CONFIG_FILE}") in ${BUILD_DIR}..."

# If no specific files passed, limit to src/ to avoid analyzing external subprojects
if [ ${#FILES[@]} -eq 0 ]; then
    exec "$RUN_CLANG_TIDY" -p "${BUILD_DIR}" -config-file="${CONFIG_FILE}" -quiet "^${REPO_ROOT}/src/.*"
else
    exec "$RUN_CLANG_TIDY" -p "${BUILD_DIR}" -config-file="${CONFIG_FILE}" -quiet "${FILES[@]}"
fi

