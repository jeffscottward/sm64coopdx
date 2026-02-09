#!/usr/bin/env python3
"""
Local development server for SM64CoopDX WebAssembly build.

Serves files from build/us_web/ with Cross-Origin headers required for
SharedArrayBuffer (COOP/COEP). These headers are needed if using pthreads
(multi-threaded mode). In single-threaded mode they are not strictly required,
but having them enables future migration to pthreads without server changes.

Usage:
    python3 serve_web.py [--port PORT] [--dir DIR]

Defaults:
    port: 8080
    dir:  build/us_web
"""

import argparse
import http.server
import os
import sys


class CORPHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler that injects Cross-Origin security headers."""

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, format, *args):
        """Override to prefix log messages."""
        sys.stderr.write("[serve_web] %s - %s\n" %
                         (self.address_string(), format % args))


def main():
    parser = argparse.ArgumentParser(
        description="Serve SM64CoopDX web build with COOP/COEP headers")
    parser.add_argument("--port", type=int, default=8080,
                        help="Port to listen on (default: 8080)")
    parser.add_argument("--dir", type=str, default="build/us_web",
                        help="Directory to serve (default: build/us_web)")
    args = parser.parse_args()

    serve_dir = os.path.abspath(args.dir)
    if not os.path.isdir(serve_dir):
        print(f"Error: directory '{serve_dir}' does not exist.", file=sys.stderr)
        print("Have you built the web version? Run: gmake -f Makefile.web -j8",
              file=sys.stderr)
        sys.exit(1)

    os.chdir(serve_dir)

    server = http.server.HTTPServer(("", args.port), CORPHandler)
    url = f"http://localhost:{args.port}/sm64coopdx.html"
    print(f"Serving {serve_dir} on port {args.port}")
    print(f"Open: {url}")
    print("Press Ctrl+C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped.")
        server.server_close()


if __name__ == "__main__":
    main()
