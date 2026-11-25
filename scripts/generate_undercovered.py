"""Test and documentation coverage analyzer and UNDERCOVERED.md generator.

Measures function, line, and branch test coverage (via gcovr) or AST-level
Doxygen documentation coverage (via libclang, when --doc is passed) across
all C source and header files and writes a markdown checkbox report to
UNDERCOVERED.md.
"""

import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

from update_shields import (
    build_and_run_coverage,
    get_clean_env,
    get_gcov_candidates,
    is_coverage_fresh,
    parse_cmake_cache,
)


def find_and_init_libclang():
    """Locate and initialize libclang across macOS, Linux, and Windows."""
    import clang.cindex

    try:
        _ = clang.cindex.Config().lib
        return
    except (clang.cindex.LibclangError, OSError):
        pass

    search_paths = [
        # macOS Homebrew & MacPorts
        "/opt/homebrew/opt/llvm/lib/libclang*.dylib",
        "/opt/homebrew/opt/llvm*/lib/libclang*.dylib",
        "/opt/homebrew/Cellar/llvm*/*/lib/libclang*.dylib",
        "/usr/local/opt/llvm*/lib/libclang*.dylib",
        "/opt/local/libexec/llvm-*/lib/libclang*.dylib",
        # macOS Xcode / CommandLineTools
        "/Library/Developer/CommandLineTools/usr/lib/libclang*.dylib",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang*.dylib",
        # Linux common locations
        "/usr/lib/llvm-*/lib/libclang-[0-9]*.so*",
        "/usr/lib/llvm-*/lib/libclang.so*",
        "/usr/lib/x86_64-linux-gnu/libclang-[0-9]*.so*",
        "/usr/lib/aarch64-linux-gnu/libclang-[0-9]*.so*",
        "/usr/lib/x86_64-linux-gnu/libclang.so*",
        "/usr/lib/aarch64-linux-gnu/libclang.so*",
        "/usr/local/lib/libclang.so*",
        "/usr/lib/libclang.so*",
        # Windows
        "C:/Program Files/LLVM/bin/libclang.dll",
        "C:/Program Files (x86)/LLVM/bin/libclang.dll",
    ]

    candidates = []
    for pattern in search_paths:
        for match in glob.glob(pattern):
            if match not in candidates and os.path.isfile(match):
                candidates.append(match)

    for match in candidates:
        if "libclang-cpp" in match:
            continue
        try:
            clang.cindex.Config.set_library_file(match)
            _ = clang.cindex.Config().lib
            return
        except (clang.cindex.LibclangError, OSError):
            continue

    for match in candidates:
        if "libclang-cpp" in match:
            continue
        try:
            clang.cindex.Config.set_library_file(match)
            clang.cindex.Config.set_compatibility_check(False)
            _ = clang.cindex.Config().lib
            return
        except (clang.cindex.LibclangError, OSError):
            continue


PARENT_TU_MAP = {
    "tests/e2e/test_abstract_struct_oom.h": "tests/e2e/test_abstract_struct.c",
    "tests/e2e/test_cdd_c_ir_oom.h": "tests/e2e/test_cdd_c_ir.c",
    "tests/e2e/test_api_helpers.h": "tests/e2e/test_api_coverage.c",
    "tests/e2e/test_api_collections.h": "tests/e2e/test_api_coverage.c",
    "tests/e2e/test_api_crud.h": "tests/e2e/test_api_coverage.c",
    "tests/e2e/test_api_exhaust.h": "tests/e2e/test_api_coverage.c",
    "tests/e2e/test_api_hydration.h": "tests/e2e/test_api_coverage.c",
    "tests/e2e/test_api_relations.h": "tests/e2e/test_api_coverage.c",
    "tests/e2e/test_api_transactions.h": "tests/e2e/test_api_coverage.c",
}


def get_compile_args():
    """Build libclang compilation flags including dependency include directories.

    Returns:
        List of compiler argument strings for clang.cindex.Index.parse.
    """
    import clang.cindex

    compile_args = [
        "-Iinclude",
        "-Isrc",
        "-Itests",
        "-Itests/e2e",
        "-Itests/e2e/pregen",
        "-Iexamples",
        "-x",
        "c",
        "-DC_ORM_TEST_ALLOCATOR",
        "-DC_ORM_ENABLE_ODBC",
        "-DC_ORM_ENABLE_DB2",
        "-DC_ORM_ENABLE_COPYBOOK",
        "-DC_ORM_ENABLE_SQLITE",
        "-DC_ORM_SQLITE_DYNAMIC",
        "-DC_ORM_ENABLE_POSTGRESQL",
        "-DC_ORM_POSTGRESQL_DYNAMIC",
        "-DC_ORM_ENABLE_MYSQL",
        "-DC_ORM_MYSQL_DYNAMIC",
        "-D_GNU_SOURCE",
        "-fparse-all-comments",
    ]
    lib_file = clang.cindex.Config.library_file
    if lib_file:
        res_dirs = sorted(
            glob.glob(os.path.join(os.path.dirname(lib_file), "clang", "*"))
        )
        if res_dirs:
            compile_args.extend(["-resource-dir", res_dirs[-1]])

    seen_inc = set()
    for bdir in ("build", "build_cov", "build_gcc", "build_clang"):
        for sub in (
            "_deps/cdd_c-src/include",
            "_deps/cdd_c-src/include/c_cdd",
            "_deps/cdd_c-src/src",
            "_deps/cdd_c-src/src/mocks",
            "_deps/cdd_c-build/src",
            "_deps/cfs-src/include",
            "_deps/c_str_span-src/include",
            "_deps/c_str_span-src/c_str_span",
            "_deps/c_str_span-build/c_str_span",
            "_deps/c89stringutils-src/include",
            "_deps/c89stringutils-src/c89stringutils",
            "_deps/c89stringutils-build",
            "_deps/greatest-src",
            "_deps/parson-src",
            "_deps/c-abstract-http-src/include",
            "tests/e2e",
        ):
            dep_inc = os.path.join(bdir, sub)
            if os.path.isdir(dep_inc) and sub not in seen_inc:
                seen_inc.add(sub)
                compile_args.append(f"-I{dep_inc}")
    if sys.platform == "darwin":
        try:
            sdk_path = subprocess.check_output(
                ["xcrun", "--show-sdk-path"], text=True
            ).strip()
            if sdk_path and os.path.exists(sdk_path):
                compile_args.extend(["-isysroot", sdk_path])
        except (subprocess.SubprocessError, OSError):
            pass
    return compile_args


def in_this_file(loc, filepath):
    """Check whether a libclang source location belongs to filepath.

    Args:
        loc: clang.cindex.SourceLocation instance.
        filepath: Path of the file currently being analyzed.

    Returns:
        True if loc refers to filepath, False otherwise.
    """
    if not loc or not loc.file:
        return False
    try:
        return os.path.samefile(loc.file.name, filepath)
    except OSError:
        return os.path.abspath(loc.file.name) == os.path.abspath(filepath)


def analyze_file(filepath, idx, compile_args):
    """Analyze Doxygen documentation coverage for a single C/H file.

    Evaluates 6 criteria: file header (@file), module (@defgroup/@addtogroup/@module),
    structs/unions, struct/union fields, functions, and function parameters.

    Args:
        filepath: Path to the C or H file to parse.
        idx: clang.cindex.Index instance.
        compile_args: List of clang parse flags.

    Returns:
        Dictionary summarizing covered/total items and missing item descriptions.
    """
    import clang.cindex

    with open(filepath, "r", encoding="utf-8", errors="ignore") as fp:
        content = fp.read()

    comment_blocks = re.findall(r"/\*[\s\S]*?\*/", content)
    has_file_doc = any(
        re.search(r"[@\\]file\b", block) for block in comment_blocks
    )

    has_module_doc = (
        any(
            re.search(r"[@\\](defgroup|addtogroup|module)\b", block)
            for block in comment_blocks
        )
        or has_file_doc
    )

    norm_rel = filepath.replace("\\", "/")
    parse_target = PARENT_TU_MAP.get(norm_rel, filepath)
    if not os.path.exists(parse_target):
        parse_target = filepath
    tu = idx.parse(parse_target, args=compile_args)

    struct_defs = {}
    func_decls = {}

    for c in tu.cursor.walk_preorder():
        if not in_this_file(c.location, filepath):
            continue

        if c.kind in (
            clang.cindex.CursorKind.STRUCT_DECL,
            clang.cindex.CursorKind.UNION_DECL,
        ) and c.is_definition():
            key = (c.location.line, c.location.column)
            if key not in struct_defs:
                struct_defs[key] = c

        elif c.kind == clang.cindex.CursorKind.FUNCTION_DECL:
            key = (c.location.line, c.location.column)
            if key not in func_decls:
                func_decls[key] = c

    missing_items = []
    total_items = 2
    covered_items = 0

    if has_file_doc:
        covered_items += 1
    else:
        missing_items.append("file doc header (`@file`)")

    if has_module_doc:
        covered_items += 1
    else:
        missing_items.append("module doc (`@defgroup` / `@module`)")

    for c in struct_defs.values():
        total_items += 1
        s_doc = bool(c.raw_comment and c.raw_comment.strip())
        sname = c.spelling
        if (
            not sname
            or sname.startswith("struct (unnamed")
            or sname.startswith("union (unnamed")
        ):
            sname = f"anonymous_struct_l{c.location.line}"
        if s_doc:
            covered_items += 1
        else:
            missing_items.append(f"struct `{sname}`")

        for child in c.get_children():
            if child.kind == clang.cindex.CursorKind.FIELD_DECL:
                total_items += 1
                fname = child.spelling or f"field_l{child.location.line}"
                f_doc = bool(child.raw_comment and child.raw_comment.strip())
                if not f_doc:
                    for sub in child.get_children():
                        if (
                            sub.kind
                            in (
                                clang.cindex.CursorKind.STRUCT_DECL,
                                clang.cindex.CursorKind.UNION_DECL,
                            )
                            and sub.raw_comment
                            and sub.raw_comment.strip()
                        ):
                            f_doc = True
                            break
                if (
                    not f_doc
                    and s_doc
                    and c.raw_comment
                    and re.search(
                        r"(@var|@param|@brief|field)\s+" + re.escape(fname) + r"\b",
                        c.raw_comment,
                    )
                ):
                    f_doc = True
                if f_doc:
                    covered_items += 1
                else:
                    missing_items.append(f"field `{sname}.{fname}`")

    for c in func_decls.values():
        total_items += 1
        fn_doc = bool(c.raw_comment and c.raw_comment.strip())
        fname = c.spelling
        if fn_doc:
            covered_items += 1
        else:
            missing_items.append(f"func `{fname}`")

        fn_comment = c.raw_comment or ""
        for idx_arg, arg in enumerate(c.get_arguments()):
            aname = arg.spelling
            total_items += 1
            if not aname:
                missing_items.append(f"param `{fname}(arg#{idx_arg + 1} unnamed)`")
            elif fn_doc and re.search(
                r"(@param(\[[^\]]*\])?|@arg|:param)\s+" + re.escape(aname) + r"\b",
                fn_comment,
            ):
                covered_items += 1
            else:
                missing_items.append(f"param `{fname}({aname})`")

    pct = (covered_items / total_items * 100.0) if total_items > 0 else 100.0
    is_fully_covered = len(missing_items) == 0

    return {
        "file": filepath,
        "is_fully_covered": is_fully_covered,
        "covered_items": covered_items,
        "total_items": total_items,
        "pct": pct,
        "missing_items": missing_items,
        "struct_total": len(struct_defs),
        "func_total": len(func_decls),
    }


def categorize_file(filepath):
    """Assign a repository C/H file path to a report category key.

    Args:
        filepath: Repository-relative file path.

    Returns:
        Category identifier string.
    """
    if filepath.startswith("include/"):
        return "headers"
    if filepath.startswith("src/legacy_cdd_db/"):
        return "legacy"
    if filepath.startswith("src/"):
        return "src"
    if filepath.startswith("examples/"):
        return "examples"
    if filepath.startswith("tests/benchmarks/"):
        return "benchmarks"
    if filepath.startswith("tests/e2e/"):
        return "e2e"
    return "tests_standalone"


def generate_report(output_path="UNDERCOVERED.md"):
    """Analyze repository files for Doxygen coverage and write UNDERCOVERED.md.

    Args:
        output_path: Destination markdown report path.
    """
    import clang.cindex

    find_and_init_libclang()
    compile_args = get_compile_args()
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

    print(f"Analyzing {len(c_files)} files for doc coverage...")
    reports = [analyze_file(f, idx, compile_args) for f in c_files]

    total_files = len(reports)
    fully_covered = [r for r in reports if r["is_fully_covered"]]
    undercovered = [r for r in reports if not r["is_fully_covered"]]

    total_items = sum(r["total_items"] for r in reports)
    covered_items = sum(r["covered_items"] for r in reports)
    overall_pct = (
        (covered_items / total_items * 100.0) if total_items > 0 else 100.0
    )

    categories = {
        "headers": ("Public API Header Files (`include/`)", []),
        "src": ("Core Library Source Files (`src/`)", []),
        "examples": ("Example Applications (`examples/`)", []),
        "e2e": ("End-to-End Test Suite (`tests/e2e/`)", []),
        "benchmarks": ("Benchmark Suite (`tests/benchmarks/`)", []),
        "tests_standalone": ("Standalone Test Fixtures (`tests/`)", []),
        "legacy": ("Legacy Test Fixtures (`src/legacy_cdd_db/`)", []),
    }

    for r in reports:
        cat_key = categorize_file(r["file"])
        categories[cat_key][1].append(r)

    doc_lines = [
        "# Undercovered Files (< 100% Doc Coverage)",
        "",
        (
            "This document tracks all files with less than 100% documentation coverage "
            "across function arguments (`arg`), functions (`function`), structures (`struct`), "
            "structure fields (`struct field`), file headers (`file`), and modules (`module`)."
        ),
        "",
        "## Documentation Coverage Summary",
        "",
        f"- **Total Files Analyzed:** {total_files}",
        (
            f"- **Fully Documented Files (100% doc coverage):** {len(fully_covered)} "
            f"({len(fully_covered) * 100.0 / total_files:.1f}%)"
        ),
        (
            f"- **Undercovered Files (< 100% doc coverage):** {len(undercovered)} "
            f"({len(undercovered) * 100.0 / total_files:.1f}%)"
        ),
        f"- **Overall Item Coverage:** {covered_items:,} / {total_items:,} ({overall_pct:.2f}%)",
        "",
        "### Summary by Category",
        "",
        "| Category | Total Files | Fully Covered (100%) | Undercovered (< 100%) | Item Coverage |",
        "| :--- | :---: | :---: | :---: | :---: |",
    ]

    for cat_title, cat_reports in categories.values():
        c_tot = len(cat_reports)
        c_full = sum(1 for r in cat_reports if r["is_fully_covered"])
        c_under = sum(1 for r in cat_reports if not r["is_fully_covered"])
        c_items = sum(r["total_items"] for r in cat_reports)
        c_cov = sum(r["covered_items"] for r in cat_reports)
        c_pct = (c_cov / c_items * 100.0) if c_items > 0 else 100.0
        clean_title = cat_title.split(" (")[0]
        doc_lines.append(
            f"| {clean_title} | {c_tot} | {c_full} | {c_under} | {c_cov}/{c_items} ({c_pct:.1f}%) |"
        )

    doc_lines.extend(
        [
            "",
            "---",
            "",
            "## Undercovered Files (< 100% Doc Coverage)",
            "",
            (
                "The following files currently have less than 100% documentation coverage across "
                "args, functions, structs, struct fields, file, and module definitions:"
            ),
            "",
        ]
    )

    if not undercovered:
        doc_lines.append(
            "*(None. All files currently achieve 100% documentation coverage.)*"
        )
        doc_lines.append("")

    section_num = 1
    for cat_title, cat_reports in categories.values():
        cat_under = [r for r in cat_reports if not r["is_fully_covered"]]
        if not cat_under:
            continue
        doc_lines.append(f"### {section_num}. {cat_title}")
        doc_lines.append("")
        for r in cat_under:
            missing_str = ", ".join(r["missing_items"][:6])
            if len(r["missing_items"]) > 6:
                missing_str += f", and {len(r['missing_items']) - 6} more"
            doc_lines.append(
                f"- [ ] `{r['file']}` — **Coverage:** {r['pct']:.1f}% "
                f"({r['covered_items']}/{r['total_items']} items) | **Missing:** {missing_str}"
            )
        doc_lines.append("")
        section_num += 1

    doc_lines.extend(
        [
            "---",
            "",
            "## Fully Documented Files (100% Doc Coverage)",
            "",
            (
                "The following files currently meet 100% documentation coverage across all "
                "functions, function arguments, structs, struct fields, and file/module headers:"
            ),
            "",
        ]
    )

    section_num = 1
    for cat_title, cat_reports in categories.values():
        cat_full = [r for r in cat_reports if r["is_fully_covered"]]
        if not cat_full:
            continue
        doc_lines.append(f"### {section_num}. {cat_title}")
        doc_lines.append("")
        for r in cat_full:
            doc_lines.append(
                f"- [x] `{r['file']}` — **Coverage:** 100.0% "
                f"({r['covered_items']}/{r['total_items']} items)"
            )
        doc_lines.append("")
        section_num += 1

    nl = chr(10)
    content = nl.join(doc_lines) + nl
    with open(output_path, "w", encoding="utf-8") as fp:
        fp.write(content)
    print(f"Successfully generated {output_path} ({len(content)} bytes)")


def resolve_cov_dir(explicit_dir=None, force_rebuild=False, skip_build=False):
    """Select and validate (or rebuild) the coverage build directory.

    Args:
        explicit_dir: Optional user-specified CMake build directory.
        force_rebuild: If True, rebuild and re-run ctest unconditionally.
        skip_build: If True, skip building/testing and use existing coverage files.

    Returns:
        Path to the validated coverage build directory.
    """
    env = get_clean_env()
    if explicit_dir:
        cov_dir = explicit_dir
    else:
        cov_dir = None
        for cand in ("build_cov", "build_gcc"):
            if is_coverage_fresh(cand):
                cov_dir = cand
                break
        if cov_dir is None:
            for cand in ("build_cov", "build_gcc"):
                if os.path.isdir(cand) and parse_cmake_cache(cand)[2]:
                    cov_dir = cand
                    break
        if cov_dir is None:
            cov_dir = "build_cov"

    if not skip_build and (force_rebuild or not is_coverage_fresh(cov_dir)):
        build_and_run_coverage(cov_dir, env)

    return cov_dir


def generate_test_report(
    output_path="UNDERCOVERED.md",
    cov_dir=None,
    force_rebuild=False,
    skip_build=False,
):
    """Generate function, line, and branch test coverage report in UNDERCOVERED.md.

    Args:
        output_path: Destination markdown report path.
        cov_dir: Optional CMake coverage build directory override.
        force_rebuild: If True, rebuild and re-run ctest even if .gcda files look fresh.
        skip_build: If True, do not invoke cmake or ctest prior to running gcovr.
    """
    cov_dir = resolve_cov_dir(
        explicit_dir=cov_dir, force_rebuild=force_rebuild, skip_build=skip_build
    )
    norm_cov = cov_dir.strip("./")

    gcov_exec = None
    for cand in get_gcov_candidates(cov_dir):
        prog = cand.split()[0]
        if shutil.which(prog):
            gcov_exec = cand
            break
    if not gcov_exec:
        gcov_exec = "gcov"

    with tempfile.TemporaryDirectory() as tmp_dir:
        cov_json = os.path.join(tmp_dir, "cov_summary.json")
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
            "--json-summary",
            cov_json,
        ]
        subprocess.run(cmd, check=True)

        with open(cov_json, "r", encoding="utf-8") as f:
            d = json.load(f)

    cov_map = {f["filename"]: f for f in d["files"]}
    for gen_models in ("tests/e2e/Models.c", f"{norm_cov}/tests/e2e/Models.c"):
        if gen_models in cov_map:
            if "tests/e2e/pregen/Models.c" not in cov_map:
                cov_map["tests/e2e/pregen/Models.c"] = cov_map.pop(gen_models)
            else:
                cov_map.pop(gen_models)

    exec_header_items = sorted(
        [k for k in cov_map if k.startswith("include/")]
    )
    src_items = sorted(
        [
            k
            for k in cov_map
            if k.startswith("src/") and not k.startswith("src/legacy_cdd_db/")
        ]
    )
    ex_items = sorted([k for k in cov_map if k.startswith("examples/")])
    models_items = (
        ["tests/e2e/pregen/Models.c"]
        if "tests/e2e/pregen/Models.c" in cov_map
        else []
    )
    e2e_items = sorted(
        [
            k
            for k in cov_map
            if k.startswith("tests/e2e/") and k != "tests/e2e/pregen/Models.c"
        ]
    )
    bm_items = sorted([k for k in cov_map if k.startswith("tests/benchmarks/")])
    legacy_src_items = sorted(
        [k for k in cov_map if k.startswith("src/legacy_cdd_db/")]
    )
    legacy_test_items = sorted(
        [
            k
            for k in cov_map
            if k.startswith("tests/")
            and not k.startswith("tests/e2e/")
            and not k.startswith("tests/benchmarks/")
        ]
    )
    legacy_all = sorted(legacy_test_items + legacy_src_items)

    git_files = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        text=True,
    ).splitlines()
    c_h_files = [
        f
        for f in git_files
        if f.endswith((".c", ".h"))
        and f.startswith(("include/", "src/", "examples/", "tests/"))
        and os.path.exists(f)
    ]
    inc_headers = sorted([f for f in c_h_files if f not in cov_map])

    all_cov_files = sorted(cov_map.keys())
    undercovered = []
    fully_covered = []

    for k in all_cov_files:
        v = cov_map[k]
        lp = v.get("line_percent", 0.0)
        fp = v.get("function_percent", 0.0)
        bp = v.get("branch_percent")
        b_tot = v.get("branch_total")
        branches_ok = b_tot is None or b_tot == 0 or bp == 100.0
        if lp == 100.0 and fp == 100.0 and branches_ok:
            fully_covered.append(k)
        else:
            undercovered.append(k)

    fully_covered_set = set(fully_covered)

    total_lines = sum(cov_map[k]["line_total"] for k in all_cov_files)
    cov_lines = sum(cov_map[k]["line_covered"] for k in all_cov_files)
    total_funcs = sum(cov_map[k]["function_total"] for k in all_cov_files)
    cov_funcs = sum(cov_map[k]["function_covered"] for k in all_cov_files)
    total_branches = sum(
        cov_map[k]["branch_total"]
        for k in all_cov_files
        if cov_map[k]["branch_total"] is not None
    )
    cov_branches = sum(
        cov_map[k]["branch_covered"]
        for k in all_cov_files
        if cov_map[k]["branch_covered"] is not None
    )

    overall_line_pct = (
        (cov_lines * 100.0 / total_lines) if total_lines > 0 else 100.0
    )
    overall_func_pct = (
        (cov_funcs * 100.0 / total_funcs) if total_funcs > 0 else 100.0
    )
    overall_branch_pct = (
        (cov_branches * 100.0 / total_branches) if total_branches > 0 else 100.0
    )

    def fmt_entry(key, checked):
        """Format a single file's line/function/branch coverage checkbox line.

        Args:
            key: Repository-relative file path key in cov_map.
            checked: True to render a checked box, False for unchecked.

        Returns:
            Formatted Markdown checkbox list entry string.
        """
        v = cov_map[key]
        lp = f"{v['line_percent']:.1f}% ({v['line_covered']}/{v['line_total']})"
        fp = f"{v['function_percent']:.1f}% ({v['function_covered']}/{v['function_total']})"
        if v["branch_total"] is not None and v["branch_total"] > 0:
            bp = f"{v['branch_percent']:.1f}% ({v['branch_covered']}/{v['branch_total']})"
        else:
            bp = "N/A (0 branches)"
        box = "x" if checked else " "
        return f"- [{box}] `{key}` — **Lines:** {lp} | **Functions:** {fp} | **Branches:** {bp}"

    cov_file_count = len(all_cov_files)
    full_pct = (
        (len(fully_covered) * 100.0 / cov_file_count) if cov_file_count > 0 else 100.0
    )
    under_pct = (
        (len(undercovered) * 100.0 / cov_file_count) if cov_file_count > 0 else 0.0
    )

    doc = [
        "# Undercovered Files (< 100% Test Coverage)",
        "",
        (
            "This document tracks test coverage across functions, lines, and branches for all codebase files. "
            "Test coverage is measured with GCC/Clang coverage instrumentation (`--coverage`) and analyzed using `gcovr` "
            "across all test suites (`e2e_test`, `perfect`, `standalone_fixtures`, `legacy_cdd_db`, `example_blog`, "
            "`example_dashboard`, `example_relationships`, `example_cobol_db2_ledger`, and `benchmarks`)."
        ),
        "",
        "## Test Coverage Summary",
        "",
        f"- **Total C/H Files in Repository:** {len(c_h_files)}",
        f"- **Executable Source & Test Files Analyzed:** {cov_file_count}",
        f"- **Fully Covered Files (100% function, line, and branch coverage):** {len(fully_covered)} ({full_pct:.1f}%)",
        f"- **Undercovered Files (< 100% test coverage):** {len(undercovered)} ({under_pct:.1f}%)",
        f"- **Declaration-Only Header Files (no executable code):** {len(inc_headers)}",
        f"- **Total Executable Lines:** {cov_lines:,} / {total_lines:,} ({overall_line_pct:.1f}%)",
        f"- **Total Executable Functions:** {cov_funcs:,} / {total_funcs:,} ({overall_func_pct:.1f}%)",
        f"- **Total Executable Branches:** {cov_branches:,} / {total_branches:,} ({overall_branch_pct:.1f}%)",
        "",
        "### Summary by Category",
        "",
        "| Category | Files | Line Coverage | Function Coverage | Branch Coverage | Status |",
        "| :--- | :---: | :---: | :---: | :---: | :---: |",
    ]

    categories = []
    if exec_header_items:
        categories.append(("Public API Headers (`include/`)", exec_header_items))
    categories.extend(
        [
            ("Core Library Sources (`src/`)", src_items),
            ("Example Applications (`examples/`)", ex_items),
            ("Generated Models (`tests/e2e/pregen/`)", models_items),
            ("End-to-End Test Suite (`tests/e2e/`)", e2e_items),
            ("Benchmark Suite (`tests/benchmarks/`)", bm_items),
            (
                "Standalone & Legacy Tests (`tests/` & `src/legacy_cdd_db/`)",
                legacy_all,
            ),
        ]
    )

    for cat_title, items in categories:
        if not items:
            continue
        l_tot = sum(cov_map[k]["line_total"] for k in items)
        l_cov = sum(cov_map[k]["line_covered"] for k in items)
        f_tot = sum(cov_map[k]["function_total"] for k in items)
        f_cov = sum(cov_map[k]["function_covered"] for k in items)
        b_tot = sum(
            cov_map[k]["branch_total"]
            for k in items
            if cov_map[k]["branch_total"] is not None
        )
        b_cov = sum(
            cov_map[k]["branch_covered"]
            for k in items
            if cov_map[k]["branch_covered"] is not None
        )
        l_pct = (l_cov * 100.0 / l_tot) if l_tot > 0 else 100.0
        f_pct = (f_cov * 100.0 / f_tot) if f_tot > 0 else 100.0
        b_pct = (b_cov * 100.0 / b_tot) if b_tot > 0 else 100.0
        b_str = (
            f"{b_cov:,} / {b_tot:,} ({b_pct:.1f}%)"
            if b_tot > 0
            else "N/A (0 branches)"
        )
        covered_in_cat = sum(1 for k in items if k in fully_covered_set)
        cat_status = (
            "100% Covered"
            if covered_in_cat == len(items)
            else f"{covered_in_cat} / {len(items)} Covered"
        )
        doc.append(
            f"| {cat_title} | {len(items)} | {l_cov:,} / {l_tot:,} ({l_pct:.1f}%) | {f_cov:,} / {f_tot:,} ({f_pct:.1f}%) | {b_str} | {cat_status} |"
        )

    doc.append(
        f"| Declaration-Only Headers (`include/`, `tests/`) | {len(inc_headers)} | N/A (0 lines) | N/A (0 funcs) | N/A (0 branches) | Declarations |"
    )
    doc.extend(
        [
            "",
            "---",
            "",
            "## Undercovered Files (< 100% Test Coverage)",
            "",
            "The following files currently have less than 100% function, line, or branch test coverage:",
            "",
        ]
    )

    if not undercovered:
        doc.append(
            "*(None. All executable source and test files currently achieve 100.0% function, line, and branch coverage.)*"
        )
        doc.append("")
    else:
        for k in undercovered:
            doc.append(fmt_entry(k, checked=False))
        doc.append("")

    doc.extend(
        [
            "---",
            "",
            "## Fully Covered Files (100% Test Coverage)",
            "",
            "The following files achieve 100.0% function, line, and branch test coverage:",
            "",
        ]
    )

    sections = []
    if exec_header_items:
        sections.append(("Public API Header Files (`include/`)", exec_header_items))
    sections.extend(
        [
            ("Core Library Source Files (`src/`)", src_items),
            ("Example Applications (`examples/`)", ex_items),
            ("Generated Model Definitions (`tests/e2e/pregen/`)", models_items),
            ("End-to-End Test Suite Files (`tests/e2e/`)", e2e_items),
            ("Benchmark Suite Files (`tests/benchmarks/`)", bm_items),
            (
                "Standalone & Legacy Test Runners and Fixtures (`tests/` & `src/legacy_cdd_db/`)",
                legacy_all,
            ),
        ]
    )

    sec_idx = 1
    for sec_title, items in sections:
        sec_covered = [k for k in items if k in fully_covered_set]
        if not sec_covered:
            continue
        doc.append(f"### {sec_idx}. {sec_title}")
        doc.append("")
        for k in sec_covered:
            doc.append(fmt_entry(k, checked=True))
        doc.append("")
        sec_idx += 1

    doc.extend(
        [
            "---",
            "",
            "## Declaration-Only Header Files (No Executable Code)",
            "",
            "The following header files declare public data structures, enums, macros, and API prototypes without containing executable inline function definitions or executable statements:",
            "",
        ]
    )

    for h in inc_headers:
        doc.append(
            f"- [x] `{h}` — Declaration-only header (no executable functions, lines, or branches)"
        )

    doc.append("")

    content = chr(10).join(doc)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Successfully generated {output_path} ({len(content)} bytes)")


def main():
    """CLI entry point for generating test or doc coverage reports."""
    args = sys.argv[1:]
    if "--doc" in args:
        generate_report()
        return

    cov_dir = None
    if "--build-dir" in args:
        idx = args.index("--build-dir")
        if idx + 1 < len(args):
            cov_dir = args[idx + 1]
    force_rebuild = "--rebuild" in args
    skip_build = "--no-build" in args
    generate_test_report(
        cov_dir=cov_dir, force_rebuild=force_rebuild, skip_build=skip_build
    )


if __name__ == "__main__":
    main()
