"""Build and run the host rules tests (src/game: Rules, Cpu, Match, Text).

    python tools/tests/run_tests.py            # the checks
    python tools/tests/run_tests.py table      # and the dealer's results table

Compiler: $CHSIM_CXX, else zig on the PATH, else the zig kept beside the
workspace (CH32Sound/.work/zig), else what tools/chsim's find_cxx() finds.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parent / "chsim"))
from chsim import find_cxx  # noqa: E402

SOURCES = [HERE / "test_rules.cpp", *sorted((ROOT / "src" / "game").glob("*.cpp"))]


def cxx():
    if not os.environ.get("CHSIM_CXX") and not shutil.which("zig"):
        work = ROOT.parent.parent / "CH32Sound" / ".work" / "zig"
        for z in sorted(work.glob("zig-*/zig.exe")) + sorted(work.glob("zig-*/zig")):
            return [str(z), "c++"]
    return find_cxx()


def main():
    exe = HERE / "build" / "test_rules.exe"
    exe.parent.mkdir(exist_ok=True)
    cmd = cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                   "-Wno-unknown-pragmas", "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                   "-DCHTEST", *[str(s) for s in SOURCES], "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    raise SystemExit(subprocess.run([str(exe), *sys.argv[1:]]).returncode)


if __name__ == "__main__":
    main()
