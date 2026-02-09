#!/usr/bin/env bash
#==============================================================================#
# build_web.sh — Build verification and convenience script for sm64coopdx web  #
#==============================================================================#
#
# This script checks all prerequisites for building sm64coopdx as a
# WebAssembly application via Emscripten, then runs the build.
#
# Usage:
#   ./build_web.sh          # Clean build with auto-detected parallelism
#   ./build_web.sh --clean  # Only clean, don't build
#   ./build_web.sh --no-clean  # Build without cleaning first
#
# Prerequisites:
#   1. Emscripten SDK (emsdk) installed and activated
#   2. baserom.us.z64 in the project root
#   3. Python 3 available in PATH
#
#==============================================================================#

set -euo pipefail

# Colors for output (disabled if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    CYAN=''
    NC=''
fi

# Script directory (project root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

#==============================================================================#
# Helper functions                                                             #
#==============================================================================#

info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

fatal() {
    error "$1"
    exit 1
}

#==============================================================================#
# Prerequisite checks                                                          #
#==============================================================================#

check_prerequisites() {
    local failed=0

    echo "=========================================="
    echo " sm64coopdx Web Build — Prerequisite Check"
    echo "=========================================="
    echo ""

    # Check 1: emcc (Emscripten C compiler)
    info "Checking for Emscripten SDK (emcc)..."
    if command -v emcc &> /dev/null; then
        local emcc_version
        emcc_version="$(emcc --version 2>&1 | head -1)"
        success "emcc found: ${emcc_version}"
    else
        error "emcc not found in PATH."
        echo "  Please install and activate the Emscripten SDK:"
        echo ""
        echo "    git clone https://github.com/emscripten-core/emsdk.git"
        echo "    cd emsdk"
        echo "    ./emsdk install latest"
        echo "    ./emsdk activate latest"
        echo "    source ./emsdk_env.sh"
        echo ""
        echo "  Then re-run this script."
        failed=1
    fi

    # Check 2: em++ (Emscripten C++ compiler)
    info "Checking for em++..."
    if command -v em++ &> /dev/null; then
        success "em++ found"
    else
        error "em++ not found in PATH."
        echo "  em++ should be available after activating the Emscripten SDK."
        failed=1
    fi

    # Check 3: emar (Emscripten archiver)
    info "Checking for emar..."
    if command -v emar &> /dev/null; then
        success "emar found"
    else
        error "emar not found in PATH."
        echo "  emar should be available after activating the Emscripten SDK."
        failed=1
    fi

    # Check 4: baserom.us.z64
    info "Checking for baserom.us.z64..."
    if [ -f "${SCRIPT_DIR}/baserom.us.z64" ]; then
        local rom_size
        rom_size="$(wc -c < "${SCRIPT_DIR}/baserom.us.z64" | tr -d ' ')"
        success "baserom.us.z64 found (${rom_size} bytes)"
    else
        error "baserom.us.z64 not found in project root."
        echo "  Please place your Super Mario 64 US ROM file at:"
        echo "    ${SCRIPT_DIR}/baserom.us.z64"
        echo ""
        echo "  The ROM must be the US version (8MB, Z64 format)."
        failed=1
    fi

    # Check 5: Python 3
    info "Checking for Python 3..."
    if command -v python3 &> /dev/null; then
        local py_version
        py_version="$(python3 --version 2>&1)"
        success "Python 3 found: ${py_version}"
    else
        error "python3 not found in PATH."
        echo "  Python 3 is required for asset extraction."
        echo "  Install it via your package manager:"
        echo "    macOS:  brew install python3"
        echo "    Ubuntu: sudo apt install python3"
        echo "    Fedora: sudo dnf install python3"
        failed=1
    fi

    # Check 6: make
    info "Checking for make..."
    if command -v make &> /dev/null; then
        local make_version
        make_version="$(make --version 2>&1 | head -1)"
        success "make found: ${make_version}"
    else
        error "make not found in PATH."
        echo "  Install it via your package manager:"
        echo "    macOS:  xcode-select --install"
        echo "    Ubuntu: sudo apt install build-essential"
        failed=1
    fi

    echo ""

    if [ "${failed}" -ne 0 ]; then
        fatal "One or more prerequisites are missing. Please fix the errors above and try again."
    fi

    success "All prerequisites satisfied!"
    echo ""
}

#==============================================================================#
# Build                                                                        #
#==============================================================================#

run_clean() {
    info "Cleaning previous web build artifacts..."
    make -f "${SCRIPT_DIR}/Makefile.web" -C "${SCRIPT_DIR}" clean-web
    success "Clean complete."
    echo ""
}

run_build() {
    # Detect number of CPU cores for parallel build
    local jobs
    if command -v nproc &> /dev/null; then
        jobs="$(nproc)"
    elif [ "$(uname)" = "Darwin" ]; then
        jobs="$(sysctl -n hw.ncpu)"
    else
        jobs=4
    fi

    info "Starting web build with ${jobs} parallel jobs..."
    echo ""

    make -f "${SCRIPT_DIR}/Makefile.web" -C "${SCRIPT_DIR}" -j"${jobs}"
}

#==============================================================================#
# Main                                                                         #
#==============================================================================#

main() {
    local do_clean=true
    local do_build=true

    # Parse arguments
    for arg in "$@"; do
        case "${arg}" in
            --clean)
                do_clean=true
                do_build=false
                ;;
            --no-clean)
                do_clean=false
                ;;
            --help|-h)
                echo "Usage: $0 [OPTIONS]"
                echo ""
                echo "Options:"
                echo "  --clean      Only clean web build artifacts, don't build"
                echo "  --no-clean   Build without cleaning first"
                echo "  --help, -h   Show this help message"
                echo ""
                echo "With no options, performs a clean build."
                exit 0
                ;;
            *)
                warn "Unknown option: ${arg}"
                ;;
        esac
    done

    # Always check prerequisites (even for clean, to give useful feedback)
    check_prerequisites

    if [ "${do_clean}" = true ]; then
        run_clean
    fi

    if [ "${do_build}" = true ]; then
        run_build
    fi
}

main "$@"
