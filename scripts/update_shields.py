import os
import sys
import shutil
import subprocess
import re


def is_tool(name):
    return shutil.which(name) is not None


def get_gcov_candidates(cov_dir):
    candidates = []
    cmake_cache = os.path.join(cov_dir, "CMakeCache.txt")
    compiler = ""
    if os.path.exists(cmake_cache):
        with open(cmake_cache, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if line.startswith("CMAKE_C_COMPILER:"):
                    compiler = line.split("=", 1)[1].strip()
                    break
    if compiler:
        comp_dir = os.path.dirname(compiler)
        comp_base = os.path.basename(compiler)
        if "gcc" in comp_base:
            m = re.search(r"gcc-(\d+)", comp_base)
            if m:
                candidates.append(f"gcov-{m.group(1)}")
                if comp_dir:
                    candidates.append(os.path.join(comp_dir, f"gcov-{m.group(1)}"))
        if comp_dir:
            candidates.append(os.path.join(comp_dir, "gcov"))

    # Xcode / LLVM candidates
    for cand in [
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-cov",
        "/opt/homebrew/opt/llvm/bin/llvm-cov",
    ]:
        if os.path.exists(cand):
            candidates.append(f"{cand} gcov")

    candidates.append("xcrun llvm-cov gcov")
    candidates.append("llvm-cov gcov")
    candidates.append("gcov")
    return candidates


def run_gcovr(cov_dir):
    if not is_tool("gcovr"):
        return None
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
            "..",
            ".",
            "--gcov-ignore-parse-errors=all",
            "-e",
            ".*_deps.*",
            "-e",
            ".*CMakeFiles.*",
            "--print-summary",
        ]
        res = subprocess.run(
            cmd,
            cwd=cov_dir,
            capture_output=True,
            text=True,
        )
        if res.returncode == 0:
            match = re.search(
                r"lines:\s+([0-9.]+)%\s+\((\d+)\s+out\s+of\s+(\d+)\)", res.stdout
            )
            if match:
                total_lines = int(match.group(3))
                if total_lines > 0:
                    return float(match.group(1))
    return None


def get_coverage():
    # Check existing build directories with gcda files
    for cand in ["build_cov", "build_gcc", "build_clang", "build"]:
        if os.path.exists(cand):
            has_gcda = False
            for root, _, files in os.walk(cand):
                if any(f.endswith(".gcda") for f in files):
                    has_gcda = True
                    break
            if has_gcda:
                cov = run_gcovr(cand)
                if cov is not None:
                    return cov

    # If no valid coverage was obtained, build build_cov and run tests
    cov_dir = "build_cov"
    try:
        if not os.path.exists(cov_dir):
            subprocess.run(
                [
                    "cmake",
                    "-S",
                    ".",
                    "-B",
                    cov_dir,
                    "-DCMAKE_BUILD_TYPE=Debug",
                    "-DCDD_CHARSET=ANSI",
                    "-DCDD_THREADING=OFF",
                    "-DC_ORM_BUILD_TESTS=ON",
                    "-DBUILD_TESTING=ON",
                    "-DC_ORM_BUILD_BENCHMARKS=ON",
                    "-DC_ORM_BUILD_EXAMPLES=ON",
                    "-DCMAKE_C_FLAGS=--coverage",
                    "-DCMAKE_EXE_LINKER_FLAGS=--coverage",
                ],
                check=True,
            )
        subprocess.run(["cmake", "--build", cov_dir, "--parallel"], check=True)
        subprocess.run(
            ["ctest", "--test-dir", cov_dir, "--output-on-failure"], check=True
        )
        return run_gcovr(cov_dir)
    except Exception as e:
        print(f"Warning: Failed to generate coverage: {e}")
        return None


def main():
    print("Updating generic shields (with custom gcovr flags)...")
    doc_cov = 0.0
    total_decls = 0
    doc_decls = 0
    if os.path.exists("include"):
        for root, _, files in os.walk("include"):
            for file in files:
                if file.endswith(".h"):
                    with open(
                        os.path.join(root, file), "r", encoding="utf-8", errors="ignore"
                    ) as f:
                        lines = f.readlines()
                    for i, line in enumerate(lines):
                        if (
                            "_API " in line
                            or line.startswith("void ")
                            or line.startswith("int ")
                            or line.startswith("char ")
                        ):
                            total_decls += 1
                            j = i - 1
                            while j >= 0 and lines[j].strip() == "":
                                j -= 1
                            if j >= 0 and ("*/" in lines[j] or "//" in lines[j]):
                                doc_decls += 1
    if total_decls > 0:
        doc_cov = (doc_decls / total_decls) * 100.0

    test_cov = get_coverage()

    def get_color(pct):
        if pct >= 90:
            return "brightgreen"
        if pct >= 80:
            return "green"
        if pct >= 70:
            return "yellowgreen"
        if pct >= 60:
            return "yellow"
        return "red"

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
        with open("README.md", "r", encoding="utf-8") as f:
            readme = f.read()

        readme = re.sub(r"\[!\[Doc Coverage\]\(.*?\)\]\(.*?\)\n?", "", readme)
        readme = re.sub(r"\[!\[Test Coverage\]\(.*?\)\]\(.*?\)\n?", "", readme)

        license_regex = r"(\[!\[License\].*?\]\(.*?\)\n?)"
        insert_str = r"\1" + doc_shield + "\n"
        if test_shield:
            insert_str += test_shield + "\n"

        readme = re.sub(license_regex, insert_str, readme, count=1)

        with open("README.md", "w", encoding="utf-8") as f:
            f.write(readme)
    sys.exit(0)


if __name__ == "__main__":
    main()
