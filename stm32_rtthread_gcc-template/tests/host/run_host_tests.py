from pathlib import Path
import os
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "build" / "host-tests"


def main() -> int:
    if BUILD_DIR.exists():
        cache = BUILD_DIR / "CMakeCache.txt"
        if cache.exists():
            cache.unlink()
        cmake_files = BUILD_DIR / "CMakeFiles"
        if cmake_files.exists():
            shutil.rmtree(cmake_files)
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    configure_cmd = ["cmake", "-S", str(ROOT), "-B", str(BUILD_DIR)]
    if os.name == "nt":
        configure_cmd.extend(["-G", "MinGW Makefiles"])

    configure = subprocess.run(
        configure_cmd,
        cwd=ROOT,
    )
    if configure.returncode != 0:
        return configure.returncode

    build = subprocess.run(
        ["cmake", "--build", str(BUILD_DIR)],
        cwd=ROOT,
    )
    if build.returncode != 0:
        return build.returncode

    test = subprocess.run(
        ["ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure"],
        cwd=ROOT,
    )
    return test.returncode


if __name__ == "__main__":
    sys.exit(main())
