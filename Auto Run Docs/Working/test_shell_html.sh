#!/bin/bash
#
# test_shell_html.sh — Validates the Emscripten shell template structure.
#
# Checks that src/pc/web/shell.html contains all required elements:
#   1. Canvas element with id="canvas"
#   2. ROM upload button/area
#   3. Dark background CSS
#   4. Loading progress bar with Module.setStatus
#   5. Module object configuration with canvas
#   6. Mobile viewport meta tags
#   7. COOP/COEP headers comment
#   8. Emscripten {{{ SCRIPT }}} placeholder
#   9. Makefile.web --shell-file reference

set -euo pipefail

SHELL_HTML="src/pc/web/shell.html"
MAKEFILE_WEB="Makefile.web"
PASS=0
FAIL=0

check() {
    local desc="$1"
    local file="$2"
    local pattern="$3"
    if grep -q "$pattern" "$file" 2>/dev/null; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Shell HTML Template Validation ==="
echo ""

# Verify file exists
if [ ! -f "$SHELL_HTML" ]; then
    echo "FAIL: $SHELL_HTML does not exist"
    exit 1
fi

echo "--- Required HTML Elements ---"
check "Canvas element with id=\"canvas\"" "$SHELL_HTML" 'id="canvas"'
check "Canvas element is a <canvas> tag" "$SHELL_HTML" '<canvas'
check "ROM upload button present" "$SHELL_HTML" 'rom-upload-btn'
check "ROM overlay section present" "$SHELL_HTML" 'rom-overlay'
check "Loading overlay section present" "$SHELL_HTML" 'loading-overlay'
check "Progress bar element present" "$SHELL_HTML" 'progress-bar-inner'
check "Error overlay section present" "$SHELL_HTML" 'error-overlay'

echo ""
echo "--- CSS Styling ---"
check "Dark background color" "$SHELL_HTML" 'background.*#111'
check "Centered canvas (flex layout)" "$SHELL_HTML" 'justify-content.*center'
check "Loading indicator styling" "$SHELL_HTML" 'loading-text'
check "Progress bar styling" "$SHELL_HTML" 'progress-bar-outer'

echo ""
echo "--- Module Configuration ---"
check "Module object defined" "$SHELL_HTML" 'var Module'
check "Module.canvas configured" "$SHELL_HTML" "getElementById.*canvas"
check "Module.setStatus defined" "$SHELL_HTML" 'setStatus.*function'
check "Module.onRuntimeInitialized defined" "$SHELL_HTML" 'onRuntimeInitialized.*function'
check "Module.print defined" "$SHELL_HTML" 'print.*function'
check "Module.printErr defined" "$SHELL_HTML" 'printErr.*function'
check "Progress parsing in setStatus" "$SHELL_HTML" 'match.*\\d'

echo ""
echo "--- Meta Tags ---"
check "Viewport meta tag" "$SHELL_HTML" 'name="viewport"'
check "Mobile web app capable" "$SHELL_HTML" 'mobile-web-app-capable'
check "Apple mobile web app capable" "$SHELL_HTML" 'apple-mobile-web-app-capable'
check "Theme color meta" "$SHELL_HTML" 'name="theme-color"'
check "Charset UTF-8" "$SHELL_HTML" 'charset="utf-8"'

echo ""
echo "--- COOP/COEP Headers ---"
check "Cross-Origin-Opener-Policy comment" "$SHELL_HTML" 'Cross-Origin-Opener-Policy'
check "Cross-Origin-Embedder-Policy comment" "$SHELL_HTML" 'Cross-Origin-Embedder-Policy'
check "SharedArrayBuffer mention" "$SHELL_HTML" 'SharedArrayBuffer'

echo ""
echo "--- Emscripten Integration ---"
check "Emscripten SCRIPT placeholder" "$SHELL_HTML" '{{{ SCRIPT }}}'
check "File accept types (.z64/.n64/.v64)" "$SHELL_HTML" '\.z64.*\.n64.*\.v64'
check "WebGL context lost handler" "$SHELL_HTML" 'webglcontextlost'

echo ""
echo "--- Makefile.web Integration ---"
check "--shell-file reference in Makefile.web" "$MAKEFILE_WEB" '\-\-shell-file.*src/pc/web/shell.html'

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="

if [ "$FAIL" -gt 0 ]; then
    echo "SOME TESTS FAILED"
    exit 1
else
    echo "ALL TESTS PASSED"
    exit 0
fi
