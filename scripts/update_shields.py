#!/usr/bin/env python3
"""Update documentation and test coverage shields in README.md.

Validates that coverage artifacts are fresh and complete before invoking gcovr
with repository-scoped filters, rebuilding and running ctest automatically when
existing coverage data is missing or stale.
"""

import importlib.util
import os
import re
import shutil
import subprocess
import sys


def is_tool(name):
    """Check whether an executable is available on PATH.

    Args:
        name: Executable name to search for.

    Returns:
        True if found on PATH, False otherwise.
    """
    return shutil.which(name) is not None


def get_clean_env():
    """Build a subprocess environment free of empty git askpass/ssh overrides.

    Returns:
        Cleaned copy of os.environ.
    """
    env = os.environ.copy()
    for key in ("GIT_ASKPASS", "SSH_ASKPASS"):
        if key in env and not env[key].strip():
            env.pop(key, None)
    if "GIT_CONFIG_COUNT" in env:
        try:
            count = int(env["GIT_CONFIG_COUNT"])
            has_empty_cmd = False
            for idx in range(count):
                k = env.get(f"GIT_CONFIG_KEY_{idx}", "")
                v = env.get(f"GIT_CONFIG_VALUE_{idx}", "")
                if k in ("core.sshCommand", "credential.helper") and v == "":
                    has_empty_cmd = True
                    break
            if has_empty_cmd:
                env.pop("GIT_CONFIG_COUNT", None)
        except ValueError:
            pass
    return env


def parse_cmake_cache(cov_dir):
    """Extract compiler path, compiler ID, and coverage flag presence from CMakeCache.txt.

    Args:
        cov_dir: Path to the CMake build directory.

    Returns:
        Tuple of (compiler_path, compiler_id, has_coverage_flags).
    """
    cmake_cache = os.path.join(cov_dir, "CMakeCache.txt")
    compiler = ""
    compiler_id = ""
    has_cov = False
    if not os.path.exists(cmake_cache):
        return compiler, compiler_id, has_cov

    cov_tokens = ("--coverage", "-fprofile-arcs", "-ftest-coverage")
    with open(cmake_cache, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("CMAKE_C_COMPILER:"):
                compiler = line.split("=", 1)[1].strip()
            elif line.startswith("CMAKE_C_COMPILER_ID:"):
                compiler_id = line.split("=", 1)[1].strip()
            elif line.startswith(
                ("CMAKE_C_FLAGS", "CMAKE_EXE_LINKER_FLAGS", "CMAKE_SHARED_LINKER_FLAGS")
            ) and any(tok in line for tok in cov_tokens):
                has_cov = True
    return compiler, compiler_id, has_cov


def get_gcov_candidates(cov_dir):
    """Construct ordered gcov executable candidates matching the build compiler.

    Args:
        cov_dir: Path to the CMake build directory.

    Returns:
        Ordered list of gcov command strings to try with gcovr.
    """
    compiler, compiler_id, _ = parse_cmake_cache(cov_dir)
    llvm_candidates = []
    for cand in (
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-cov",
        "/opt/homebrew/opt/llvm/bin/llvm-cov",
    ):
        if os.path.exists(cand):
            llvm_candidates.append(f"{cand} gcov")
    llvm_candidates.append("xcrun llvm-cov gcov")
    llvm_candidates.append("llvm-cov gcov")

    gnu_candidates = []
    if compiler:
        comp_dir = os.path.dirname(compiler)
        comp_base = os.path.basename(compiler)
        if "gcc" in comp_base:
            m = re.search(r"gcc-(\d+)", comp_base)
            if m:
                gnu_candidates.append(f"gcov-{m.group(1)}")
                if comp_dir:
                    gnu_candidates.append(os.path.join(comp_dir, f"gcov-{m.group(1)}"))
        if comp_dir:
            gnu_candidates.append(os.path.join(comp_dir, "gcov"))
    gnu_candidates.append("gcov")

    is_clang = "Clang" in compiler_id or "clang" in os.path.basename(compiler).lower()
    ordered = (
        (llvm_candidates + gnu_candidates)
        if is_clang
        else (gnu_candidates + llvm_candidates)
    )
    deduped = []
    for item in ordered:
        if item not in deduped:
            deduped.append(item)
    return deduped


def get_latest_source_mtime():
    """Find the most recent modification timestamp across repository C/H files.

    Returns:
        Maximum mtime float across include/, src/, examples/, and tests/.
    """
    latest = 0.0
    for folder in ("include", "src", "examples", "tests"):
        if not os.path.isdir(folder):
            continue
        for root, _, files in os.walk(folder):
            for fname in files:
                if fname.endswith((".c", ".h")):
                    try:
                        mtime = os.path.getmtime(os.path.join(root, fname))
                        latest = max(latest, mtime)
                    except OSError:
                        continue
    return latest


def is_coverage_fresh(cov_dir):
    """Verify that a build directory has complete, up-to-date .gcno/.gcda coverage data.

    Ensures --coverage was enabled, e2e tests were actually executed (not merely
    build-time c-orm-cli codegen), every .gcno outside _deps has a matching .gcda
    that is not older than its .gcno, and no C/H source file was edited after
    coverage was recorded.

    Args:
        cov_dir: Path to the candidate CMake build directory.

    Returns:
        True if coverage data in cov_dir is complete and up to date, False otherwise.
    """
    if not os.path.isdir(cov_dir):
        return False
    _, _, has_cov = parse_cmake_cache(cov_dir)
    if not has_cov:
        return False

    gcno_files = {}
    gcda_files = {}
    has_e2e_gcda = False

    for root, dirs, files in os.walk(cov_dir):
        dirs[:] = [d for d in dirs if d != "_deps" and "_deps" not in d]
        if "_deps" in os.path.normpath(root).split(os.sep):
            continue
        for fname in files:
            full = os.path.join(root, fname)
            if fname.endswith(".gcno") and "CompilerIdC" not in full:
                stem = full[:-5]
                gcno_files[stem] = full
            elif fname.endswith(".gcda") and "CompilerIdC" not in full:
                stem = full[:-5]
                gcda_files[stem] = full
                if "test_e2e.c.gcda" in fname:
                    has_e2e_gcda = True

    if not gcno_files or not gcda_files or not has_e2e_gcda:
        return False

    min_gcda_mtime = None
    for stem, gcno_path in gcno_files.items():
        gcda_path = gcda_files.get(stem)
        if not gcda_path:
            return False
        try:
            gcno_mtime = os.path.getmtime(gcno_path)
            gcda_mtime = os.path.getmtime(gcda_path)
        except OSError:
            return False
        if gcda_mtime + 1.0 < gcno_mtime:
            return False
        if min_gcda_mtime is None or gcda_mtime < min_gcda_mtime:
            min_gcda_mtime = gcda_mtime

    if min_gcda_mtime is None:
        return False

    latest_src_mtime = get_latest_source_mtime()
    return latest_src_mtime <= min_gcda_mtime + 1.0


def clean_coverage_artifacts(cov_dir):
    """Remove stale .gcda files across cov_dir and any leftover .gcno/.gcda inside _deps.

    Args:
        cov_dir: Path to the CMake build directory to clean before running tests.
    """
    if not os.path.isdir(cov_dir):
        return
    for root, _, files in os.walk(cov_dir):
        in_deps = "_deps" in os.path.normpath(root).split(os.sep)
        for fname in files:
            if fname.endswith(".gcda") or (in_deps and fname.endswith(".gcno")):
                try:
                    os.remove(os.path.join(root, fname))
                except OSError:
                    pass


def run_gcovr(cov_dir):
    """Execute gcovr scoped to repository directories and return minimum coverage %.

    Computes line, function, and branch coverage across include/, src/, examples/,
    and tests/, returning min(lines, functions, branches) so the badge accurately
    reflects full test coverage.

    Args:
        cov_dir: Path to the CMake build directory containing fresh coverage data.

    Returns:
        Float coverage percentage (0.0 - 100.0), or None if gcovr could not run.
    """
    if not is_tool("gcovr"):
        return None
    norm_cov = cov_dir.strip("./")
    candidates = get_gcov_candidates(cov_dir)
    for gcov_exec in candidates:
        prog = gcov_exec.split()[0]
        if not shutil.which(prog):
            continue
        cmd = [
            "gcovr",
            "--gcov-executable",
            gcov_exec,
            "-r",
            ".",
            cov_dir,
            "--gcov-exclude-directories",
            ".*_deps.*",
            "--gcov-ignore-errors=all",
            "--gcov-ignore-parse-errors=all",
            "--exclude-unreachable-branches",
            "--exclude-throw-branches",
            "-e",
            ".*_deps.*",
            "-e",
            ".*CMakeFiles.*",
            "--filter",
            "include/.*",
            "--filter",
            "src/.*",
            "--filter",
            "examples/.*",
            "--filter",
            "tests/.*",
            "--filter",
            f"{norm_cov}/tests/.*",
            "--print-summary",
        ]
        res = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        if res.returncode == 0:
            line_m = re.search(
                r"lines:\s+([0-9.]+)%\s+\((\d+)\s+out\s+of\s+(\d+)\)", res.stdout
            )
            if not line_m:
                continue
            total_lines = int(line_m.group(3))
            if total_lines <= 0:
                continue
            metrics = [float(line_m.group(1))]

            func_m = re.search(
                r"functions:\s+([0-9.]+)%\s+\((\d+)\s+out\s+of\s+(\d+)\)", res.stdout
            )
            if func_m and int(func_m.group(3)) > 0:
                metrics.append(float(func_m.group(1)))

            branch_m = re.search(
                r"branches:\s+([0-9.]+)%\s+\((\d+)\s+out\s+of\s+(\d+)\)", res.stdout
            )
            if branch_m and int(branch_m.group(3)) > 0:
                metrics.append(float(branch_m.group(1)))

            return min(metrics)
    return None


def build_and_run_coverage(cov_dir, env):
    """Configure (if needed), clean stale .gcda files, build, and execute ctest.

    Args:
        cov_dir: Target CMake build directory.
        env: Subprocess environment dictionary.
    """
    _, _, has_cov = parse_cmake_cache(cov_dir)
    if not os.path.exists(cov_dir) or not has_cov:
        subprocess.run(
            [
                "cmake",
                "-S",
                ".",
                "-B",
                cov_dir,
                "-DCMAKE_BUILD_TYPE=Debug",
                "-DC_ORM_BUILD_TESTS=ON",
                "-DBUILD_TESTING=ON",
                "-DC_ORM_BUILD_BENCHMARKS=ON",
                "-DC_ORM_BUILD_EXAMPLES=ON",
                "-DCMAKE_C_FLAGS=--coverage",
                "-DCMAKE_EXE_LINKER_FLAGS=--coverage",
            ],
            env=env,
            check=True,
        )
    clean_coverage_artifacts(cov_dir)
    subprocess.run(["cmake", "--build", cov_dir, "--parallel"], env=env, check=True)
    subprocess.run(
        ["ctest", "--test-dir", cov_dir, "--output-on-failure"], env=env, check=True
    )


def get_coverage():
    """Obtain verified test coverage percentage, rebuilding if coverage data is stale.

    Prioritizes build_gcc (populated by pre-commit's build-gcc / test-gcc hooks)
    followed by build_cov, validating artifact freshness before accepting cached data.

    Returns:
        Float test coverage percentage, or None on failure.
    """
    for cand in ("build_gcc", "build_cov"):
        if is_coverage_fresh(cand):
            cov = run_gcovr(cand)
            if cov is not None:
                return cov

    env = get_clean_env()
    target_dir = "build_cov"
    for cand in ("build_gcc", "build_cov"):
        if os.path.isdir(cand) and parse_cmake_cache(cand)[2]:
            target_dir = cand
            break

    try:
        build_and_run_coverage(target_dir, env)
        return run_gcovr(target_dir)
    except (subprocess.SubprocessError, OSError) as exc:
        print(f"Warning: Failed to generate coverage in {target_dir}: {exc}")
        return None


def get_doc_coverage():
    """Compute documentation coverage percentage across repository headers/sources.

    Uses libclang AST analysis from generate_undercovered when available, falling
    back to a declaration comment scanner over include/*.h in minimal environments.

    Returns:
        Float documentation coverage percentage (0.0 - 100.0).
    """
    if importlib.util.find_spec("clang") is not None:
        try:
            script_dir = os.path.dirname(os.path.abspath(__file__))
            if script_dir not in sys.path:
                sys.path.insert(0, script_dir)
            import clang.cindex
            import generate_undercovered as gu

            gu.find_and_init_libclang()
            compile_args = gu.get_compile_args()
            idx = clang.cindex.Index.create()
            git_files = subprocess.check_output(
                ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
                text=True,
            ).splitlines()
            c_files = sorted(
                [
                    f
                    for f in git_files
                    if f.endswith((".c", ".h"))
                    and f.startswith(("include/", "src/", "examples/", "tests/"))
                    and os.path.exists(f)
                ]
            )
            if c_files:
                tot_items = 0
                cov_items = 0
                for fpath in c_files:
                    rep = gu.analyze_file(fpath, idx, compile_args)
                    tot_items += rep["total_items"]
                    cov_items += rep["covered_items"]
                if tot_items > 0:
                    return (cov_items / tot_items) * 100.0
        except (
            ImportError,
            OSError,
            RuntimeError,
            subprocess.SubprocessError,
        ) as exc:
            print(f"Note: Falling back to header declaration doc scanner ({exc})")

    total_decls = 0
    doc_decls = 0
    decl_prefixes = (
        "C_ORM_EXPORT ",
        "NO_DISCARD ",
        "c_orm_error_t ",
        "void ",
        "int ",
        "char ",
        "size_t ",
        "typedef struct ",
        "typedef enum ",
        "struct ",
        "enum ",
    )
    if os.path.exists("include"):
        for root, _, files in os.walk("include"):
            for file in files:
                if not file.endswith(".h"):
                    continue
                with open(
                    os.path.join(root, file), "r", encoding="utf-8", errors="ignore"
                ) as f:
                    lines = f.readlines()
                for i, line in enumerate(lines):
                    if line[:1].isspace():
                        continue
                    stripped = line.strip()
                    if stripped.startswith("#"):
                        continue
                    if "_API " in stripped or stripped.startswith(decl_prefixes):
                        if ";" in stripped and "(" not in stripped and "typedef" not in stripped:
                            continue
                        total_decls += 1
                        j = i - 1
                        while j >= 0 and (
                            lines[j].strip() == ""
                            or lines[j].lstrip().startswith("#")
                            or lines[j].lstrip().startswith("__attribute__")
                            or lines[j].strip() in ("C_ORM_EXPORT", "NO_DISCARD")
                        ):
                            j -= 1
                        if j >= 0 and ("*/" in lines[j] or "//" in lines[j]):
                            doc_decls += 1
    return (doc_decls / total_decls) * 100.0 if total_decls > 0 else 0.0


def get_color(pct):
    """Map a coverage percentage to a shields.io badge color name.

    Args:
        pct: Coverage percentage float.

    Returns:
        Shields.io color identifier string.
    """
    if pct >= 90:
        return "brightgreen"
    if pct >= 80:
        return "green"
    if pct >= 70:
        return "yellowgreen"
    if pct >= 60:
        return "yellow"
    return "red"


def main():
    """Compute doc and test coverage and update shields in README.md."""
    print("Updating README coverage shields...")
    doc_cov = get_doc_coverage()
    test_cov = get_coverage()

    doc_color = get_color(doc_cov)
    doc_shield = f"[![Doc Coverage](https://img.shields.io/badge/docs-{doc_cov:.0f}%25-{doc_color}.svg)](#)"

    test_shield = ""
    if test_cov is not None:
        test_color = get_color(test_cov)
        test_shield = f"[![Test Coverage](https://img.shields.io/badge/coverage-{test_cov:.0f}%25-{test_color}.svg)](#)"
    elif os.path.exists("README.md"):
        with open("README.md", "r", encoding="utf-8") as f:
            existing = re.search(r"(\[!\[Test Coverage\]\(.*?\)\]\(.*?\))", f.read())
            if existing:
                test_shield = existing.group(1)

    if os.path.exists("README.md"):
        nl = chr(10)
        with open("README.md", "r", encoding="utf-8") as f:
            raw_lines = f.read().splitlines()

        out_lines = []
        inserted = False
        for line in raw_lines:
            if line.startswith(("[![Doc Coverage](", "[![Test Coverage](")):
                continue
            out_lines.append(line)
            if not inserted and line.startswith("[![License]"):
                out_lines.append(doc_shield)
                if test_shield:
                    out_lines.append(test_shield)
                inserted = True

        with open("README.md", "w", encoding="utf-8") as f:
            f.write(nl.join(out_lines) + nl)
    sys.exit(0)


if __name__ == "__main__":
    main()
