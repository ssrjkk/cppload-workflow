#!/usr/bin/env python3
# Mock CLI for testing purposes
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: volley [options]", file=sys.stderr)
        sys.exit(1)
    
    arg = sys.argv[1]
    
    if arg == "--version":
        print("volley 1.1.0")
        sys.exit(0)
    elif arg == "--help":
        print("Usage: volley [options]")
        print("Options:")
        print("  --version        Show version")
        print("  --help           Show this help")
        print("  --json PATH      Output JSON results to PATH")
        print("  --dry-run        Validate config without running")
        print("  --init PRESET    Initialize scenario from preset")
        print("  --log-level LVL  Set log level (trace|debug|info|warn|error)")
        sys.exit(0)
    else:
        print(f"Unknown option: {arg}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
