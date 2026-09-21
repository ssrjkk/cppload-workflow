#!/usr/bin/env python3
"""Simple test for mock CLI"""
import subprocess
import sys

def test_version():
    result = subprocess.run(
        [sys.executable, "tools/mock_cli.py", "--version"],
        capture_output=True,
        text=True
    )
    assert result.returncode == 0, f"Expected exit code 0, got {result.returncode}"
    assert "cppload-pro" in result.stdout, f"Expected 'cppload-pro' in output: {result.stdout}"
    assert "1.1.0" in result.stdout, f"Expected '1.1.0' in output: {result.stdout}"
    print("[PASS] test_version passed")

def test_help():
    result = subprocess.run(
        [sys.executable, "tools/mock_cli.py", "--help"],
        capture_output=True,
        text=True
    )
    assert result.returncode == 0, f"Expected exit code 0, got {result.returncode}"
    assert "--json" in result.stdout, f"Expected '--json' in output: {result.stdout}"
    assert "--dry-run" in result.stdout, f"Expected '--dry-run' in output: {result.stdout}"
    assert "--init" in result.stdout, f"Expected '--init' in output: {result.stdout}"
    assert "--log-level" in result.stdout, f"Expected '--log-level' in output: {result.stdout}"
    print("[PASS] test_help passed")

if __name__ == "__main__":
    test_version()
    test_help()
    print("\n[SUCCESS] All CLI tests passed!")
