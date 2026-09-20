"""Doc coverage analyzer and undercovered markdown report generator.

Analyzes doc (arg, function, struct, struct field, file, module) coverage across
all C source and header files and generates UNDERCOVERED.md.
"""

import glob
import os
import re
import subprocess
import sys

import clang.cindex


def find_and_init_libclang():
    """Locate and initialize libclang across platforms."""
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

    # Fallback without version checks
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


def get_compile_args():
    """Build compilation arguments including include directories and sysroot."""
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
        "-D_GNU_SOURCE",
        "-fparse-all-comments",
    ]
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
    """Check if cursor location matches the file under evaluation."""
    if not loc or not loc.file:
        return False
    try:
        return os.path.samefile(loc.file.name, filepath)
    except OSError:
        return os.path.abspath(loc.file.name) == os.path.abspath(filepath)


def analyze_file(filepath, idx, compile_args):
    """Analyze documentation coverage for a single file across all 6 criteria."""
    with open(filepath, "r", encoding="utf-8", errors="ignore") as fp:
        content = fp.read()

    # 1. file documentation (@file tag or top-level file doc comment)
    has_file_doc = bool(
        re.search(r"/\*\*[\s\S]*?@file[\s\S]*?\*/", content)
    ) or bool(re.search(r"/\*[\s\S]*?@file[\s\S]*?\*/", content))

    # 2. module documentation (@defgroup / @addtogroup / @module, or @file if file is module)
    has_module_doc = bool(
        re.search(r"/\*\*?[\s\S]*?@(defgroup|addtogroup|module)[\s\S]*?\*/", content)
    ) or has_file_doc

    tu = idx.parse(filepath, args=compile_args)

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
    total_items = 2  # 1 for file doc, 1 for module doc
    covered_items = 0

    if has_file_doc:
        covered_items += 1
    else:
        missing_items.append("file doc header (`@file`)")

    if has_module_doc:
        covered_items += 1
    else:
        missing_items.append("module doc (`@defgroup` / `@module`)")

    # 3. struct & 4. struct field documentation
    for c in struct_defs.values():
        total_items += 1
        s_doc = bool(c.raw_comment and c.raw_comment.strip())
        sname = c.spelling or f"anonymous_struct_l{c.location.line}"
        if s_doc:
            covered_items += 1
        else:
            missing_items.append(f"struct `{sname}`")

        for child in c.get_children():
            if child.kind == clang.cindex.CursorKind.FIELD_DECL:
                total_items += 1
                fname = child.spelling or f"field_l{child.location.line}"
                f_doc = bool(child.raw_comment and child.raw_comment.strip())
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

    # 5. function & 6. arg documentation
    for c in func_decls.values():
        total_items += 1
        fn_doc = bool(c.raw_comment and c.raw_comment.strip())
        fname = c.spelling
        if fn_doc:
            covered_items += 1
        else:
            missing_items.append(f"func `{fname}`")

        fn_comment = c.raw_comment or ""
        for arg in c.get_arguments():
            aname = arg.spelling
            if not aname:
                continue
            total_items += 1
            if fn_doc and re.search(
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
    """Categorize file for report organization."""
    if filepath.startswith("include/"):
        return "headers"
    elif filepath.startswith("src/legacy_cdd_db/"):
        return "legacy"
    elif filepath.startswith("src/"):
        return "src"
    elif filepath.startswith("examples/"):
        return "examples"
    elif filepath.startswith("tests/benchmarks/"):
        return "benchmarks"
    elif filepath.startswith("tests/e2e/"):
        return "e2e"
    else:
        return "tests_standalone"


def generate_report():
    """Analyze repository files and generate UNDERCOVERED.md."""
    find_and_init_libclang()
    compile_args = get_compile_args()
    idx = clang.cindex.Index.create()

    git_files = subprocess.check_output(["git", "ls-files"], text=True).splitlines()
    c_files = sorted([f for f in git_files if f.endswith((".c", ".h"))])

    print(f"Analyzing {len(c_files)} files for doc coverage...")
    reports = []
    for f in c_files:
        rep = analyze_file(f, idx, compile_args)
        reports.append(rep)

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

    doc_lines = []
    doc_lines.append("# Undercovered Files (< 100% Doc Coverage)")
    doc_lines.append("")
    doc_lines.append(
        "This document tracks all files with less than 100% documentation coverage "
        "across function arguments (`arg`), functions (`function`), structures (`struct`), "
        "structure fields (`struct field`), file headers (`file`), and modules (`module`)."
    )
    doc_lines.append("")
    doc_lines.append("## Documentation Coverage Summary")
    doc_lines.append("")
    doc_lines.append(f"- **Total Files Analyzed:** {total_files}")
    doc_lines.append(
        f"- **Fully Documented Files (100% doc coverage):** {len(fully_covered)} "
        f"({len(fully_covered)*100.0/total_files:.1f}%)"
    )
    doc_lines.append(
        f"- **Undercovered Files (< 100% doc coverage):** {len(undercovered)} "
        f"({len(undercovered)*100.0/total_files:.1f}%)"
    )
    doc_lines.append(
        f"- **Overall Item Coverage:** {covered_items:,} / {total_items:,} ({overall_pct:.2f}%)"
    )
    doc_lines.append("")
    doc_lines.append("### Summary by Category")
    doc_lines.append("")
    doc_lines.append(
        "| Category | Total Files | Fully Covered (100%) | Undercovered (< 100%) | Item Coverage |"
    )
    doc_lines.append(
        "| :--- | :---: | :---: | :---: | :---: |"
    )

    for cat_key, (cat_title, cat_reports) in categories.items():
        c_tot = len(cat_reports)
        c_full = sum(1 for r in cat_reports if r["is_fully_covered"])
        c_under = sum(1 for r in cat_reports if not r["is_fully_covered"])
        c_items = sum(r["total_items"] for r in cat_reports)
        c_cov = sum(r["covered_items"] for r in cat_reports)
        c_pct = (c_cov / c_items * 100.0) if c_items > 0 else 100.0
        # Clean title for table
        clean_title = cat_title.split(" (")[0]
        doc_lines.append(
            f"| {clean_title} | {c_tot} | {c_full} | {c_under} | {c_cov}/{c_items} ({c_pct:.1f}%) |"
        )

    doc_lines.append("")
    doc_lines.append("---")
    doc_lines.append("")
    doc_lines.append("## Undercovered Files (< 100% Doc Coverage)")
    doc_lines.append("")
    doc_lines.append(
        "The following files currently have less than 100% documentation coverage across "
        "args, functions, structs, struct fields, file, and module definitions:"
    )
    doc_lines.append("")

    if not undercovered:
        doc_lines.append(
            "*(None. All files currently achieve 100% documentation coverage.)*"
        )
        doc_lines.append("")

    section_num = 1
    for cat_key, (cat_title, cat_reports) in categories.items():
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

    doc_lines.append("---")
    doc_lines.append("")
    doc_lines.append("## Fully Documented Files (100% Doc Coverage)")
    doc_lines.append("")
    doc_lines.append(
        "The following files currently meet 100% documentation coverage across all "
        "functions, function arguments, structs, struct fields, and file/module headers:"
    )
    doc_lines.append("")

    section_num = 1
    for cat_key, (cat_title, cat_reports) in categories.items():
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

    output_path = "UNDERCOVERED.md"
    content = "\n".join(doc_lines) + "\n"
    with open(output_path, "w", encoding="utf-8") as fp:
        fp.write(content)

    print(f"Successfully generated {output_path} ({len(content)} bytes)")


if __name__ == "__main__":
    generate_report()
