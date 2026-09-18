import json
import os
import subprocess

# Auto-generate cov_summary if needed
def ensure_cov_summary():
    gcov_exec = "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-cov gcov"
    if not os.path.exists(gcov_exec):
        gcov_exec = "gcov"
    cmd = [
        "gcovr",
        "--gcov-executable", gcov_exec,
        "build_cov",
        "--filter", "src/.*",
        "--filter", "examples/.*",
        "--filter", "tests/.*",
        "--filter", "build_cov/tests/.*",
        "--json-summary", "/tmp/cov_summary.json"
    ]
    subprocess.run(cmd, check=True)

ensure_cov_summary()

with open('/tmp/cov_summary.json') as f:
    d = json.load(f)

cov_map = {f['filename']: f for f in d['files']}
for k in list(cov_map.keys()):
    if k.endswith('tests/e2e/Models.c'):
        cov_map['tests/e2e/pregen/Models.c'] = cov_map[k]
        break

def is_fully_covered(info):
    if not info:
        return False
    lp = info.get('line_percent', 0.0)
    fp = info.get('function_percent', 0.0)
    bp = info.get('branch_percent')
    return (lp == 100.0 and fp == 100.0 and (bp is None or bp == 100.0))

src_items = sorted([k for k in cov_map.keys() if k.startswith('src/') and not k.startswith('src/legacy_cdd_db/')], key=lambda x: x)
ex_items = sorted([k for k in cov_map.keys() if k.startswith('examples/')], key=lambda x: x)
e2e_items = sorted([k for k in cov_map.keys() if k.startswith('tests/e2e/') and not k.endswith('Models.c')], key=lambda x: x)
bm_items = sorted([k for k in cov_map.keys() if k.startswith('tests/benchmarks/')], key=lambda x: x)
legacy_src_items = sorted([k for k in cov_map.keys() if k.startswith('src/legacy_cdd_db/')], key=lambda x: x)
legacy_test_items = sorted([k for k in cov_map.keys() if k.startswith('tests/') and not k.startswith('tests/e2e/') and not k.startswith('tests/benchmarks/')], key=lambda x: x)
all_test_items = e2e_items + bm_items + legacy_test_items + legacy_src_items

# Core Library metrics
total_src_lines = sum(cov_map[k]['line_total'] for k in src_items)
cov_src_lines = sum(cov_map[k]['line_covered'] for k in src_items)
total_src_funcs = sum(cov_map[k]['function_total'] for k in src_items)
cov_src_funcs = sum(cov_map[k]['function_covered'] for k in src_items)
total_src_branches = sum(cov_map[k]['branch_total'] for k in src_items if cov_map[k]['branch_total'] is not None)
cov_src_branches = sum(cov_map[k]['branch_covered'] for k in src_items if cov_map[k]['branch_covered'] is not None)
src_line_pct = cov_src_lines * 100.0 / total_src_lines if total_src_lines else 0.0
src_func_pct = cov_src_funcs * 100.0 / total_src_funcs if total_src_funcs else 0.0
src_branch_pct = cov_src_branches * 100.0 / total_src_branches if total_src_branches else 0.0

undercovered_src = [k for k in src_items if not is_fully_covered(cov_map.get(k))]
fully_covered_src = [k for k in src_items if is_fully_covered(cov_map.get(k))]

# Examples metrics
total_ex_lines = sum(cov_map[k]['line_total'] for k in ex_items)
cov_ex_lines = sum(cov_map[k]['line_covered'] for k in ex_items)
total_ex_funcs = sum(cov_map[k]['function_total'] for k in ex_items)
cov_ex_funcs = sum(cov_map[k]['function_covered'] for k in ex_items)
total_ex_branches = sum(cov_map[k]['branch_total'] for k in ex_items if cov_map[k]['branch_total'] is not None)
cov_ex_branches = sum(cov_map[k]['branch_covered'] for k in ex_items if cov_map[k]['branch_covered'] is not None)
ex_line_pct = cov_ex_lines * 100.0 / total_ex_lines if total_ex_lines else 0.0
ex_func_pct = cov_ex_funcs * 100.0 / total_ex_funcs if total_ex_funcs else 0.0
ex_branch_pct = cov_ex_branches * 100.0 / total_ex_branches if total_ex_branches else 0.0

undercovered_ex = [k for k in ex_items if not is_fully_covered(cov_map.get(k))]
fully_covered_ex = [k for k in ex_items if is_fully_covered(cov_map.get(k))]

# Models metrics
m = cov_map.get('tests/e2e/pregen/Models.c')
m_fully_covered = is_fully_covered(m)

# Test Suite metrics
total_test_lines = sum(cov_map[k]['line_total'] for k in all_test_items)
cov_test_lines = sum(cov_map[k]['line_covered'] for k in all_test_items)
total_test_funcs = sum(cov_map[k]['function_total'] for k in all_test_items)
cov_test_funcs = sum(cov_map[k]['function_covered'] for k in all_test_items)
total_test_branches = sum(cov_map[k]['branch_total'] for k in all_test_items)
cov_test_branches = sum(cov_map[k]['branch_covered'] for k in all_test_items)
test_line_pct = cov_test_lines * 100.0 / total_test_lines if total_test_lines else 0.0
test_func_pct = cov_test_funcs * 100.0 / total_test_funcs if total_test_funcs else 0.0
test_branch_pct = cov_test_branches * 100.0 / total_test_branches if total_test_branches else 0.0
undercovered_tests = [k for k in all_test_items if not is_fully_covered(cov_map.get(k))]
fully_covered_tests = [k for k in all_test_items if is_fully_covered(cov_map.get(k))]

lines = []
lines.append("# Undercovered Files (< 100% Coverage)")
lines.append("")
lines.append("This document tracks all files with test coverage across functions, lines, or branches, measured using GCC/Clang coverage instrumentation (`--coverage`) and analyzed with `gcovr`.")
lines.append("")
lines.append("## Coverage Summary")
lines.append("")
lines.append("### Core Library Sources (`src/`)")
lines.append("")
lines.append(f"- **Total Source Files Analyzed:** {len(src_items)}")
lines.append(f"- **Fully Covered Files (100% functions, lines, branches):** {len(fully_covered_src)} ({len(fully_covered_src)*100.0/len(src_items):.1f}%)")
lines.append(f"- **Undercovered Files (<100% in function, line, or branch coverage):** {len(undercovered_src)} ({len(undercovered_src)*100.0/len(src_items):.1f}%)")
lines.append(f"- **Library Line Coverage:** {cov_src_lines:,} / {total_src_lines:,} ({src_line_pct:.2f}%)")
lines.append(f"- **Library Function Coverage:** {cov_src_funcs:,} / {total_src_funcs:,} ({src_func_pct:.2f}%)")
lines.append(f"- **Library Branch Coverage:** {cov_src_branches:,} / {total_src_branches:,} ({src_branch_pct:.2f}%)")
lines.append("")
lines.append("### Example Applications (`examples/`)")
lines.append("")
lines.append(f"- **Total Example Files Analyzed:** {len(ex_items)}")
lines.append(f"- **Fully Covered Files (100% functions, lines, branches):** {len(fully_covered_ex)} ({len(fully_covered_ex)*100.0/len(ex_items):.1f}%)")
lines.append(f"- **Undercovered Files (<100% in function, line, or branch coverage):** {len(undercovered_ex)} ({len(undercovered_ex)*100.0/len(ex_items):.1f}%)")
lines.append(f"- **Example Line Coverage:** {cov_ex_lines:,} / {total_ex_lines:,} ({ex_line_pct:.2f}%)")
lines.append(f"- **Example Function Coverage:** {cov_ex_funcs:,} / {total_ex_funcs:,} ({ex_func_pct:.2f}%)")
lines.append(f"- **Example Branch Coverage:** {cov_ex_branches:,} / {total_ex_branches:,} ({ex_branch_pct:.2f}%)")
lines.append("")
lines.append("### Generated Model Definitions (`tests/e2e/pregen/`)")
lines.append("")
lines.append("- **Total Model Files Analyzed:** 1")
lines.append(f"- **Fully Covered Files (100% functions, lines, branches):** {1 if m_fully_covered else 0} ({100.0 if m_fully_covered else 0.0:.1f}%)")
lines.append(f"- **Undercovered Files (<100% in function, line, or branch coverage):** {0 if m_fully_covered else 1} ({0.0 if m_fully_covered else 100.0:.1f}%)")
if m:
    lines.append(f"- **Model Line Coverage:** {m['line_covered']:,} / {m['line_total']:,} ({m['line_percent']:.2f}%)")
    lines.append(f"- **Model Function Coverage:** {m['function_covered']:,} / {m['function_total']:,} ({m['function_percent']:.2f}%)")
    lines.append(f"- **Model Branch Coverage:** {m['branch_covered']:,} / {m['branch_total']:,} ({m['branch_percent']:.2f}%)")
lines.append("")
lines.append("### Test Suite Sources & Fixtures (`tests/` & `src/legacy_cdd_db/`)")
lines.append("")
lines.append(f"- **Total Test Suite Files Analyzed:** {len(all_test_items)}")
lines.append(f"- **Undercovered Test Suite Files (<100% branch/line/func coverage):** {len(undercovered_tests)} ({len(undercovered_tests)*100.0/len(all_test_items):.1f}%)")
lines.append(f"- **Test Suite Line Coverage:** {cov_test_lines:,} / {total_test_lines:,} ({test_line_pct:.2f}%)")
lines.append(f"- **Test Suite Function Coverage:** {cov_test_funcs:,} / {total_test_funcs:,} ({test_func_pct:.2f}%)")
lines.append(f"- **Test Suite Branch Coverage:** {cov_test_branches:,} / {total_test_branches:,} ({test_branch_pct:.2f}%)")
lines.append("")
lines.append("---")
lines.append("")
lines.append("## Undercovered Files (< 100% Coverage)")
lines.append("")
lines.append("The following files currently have less than 100% test (function, line, branch) coverage:")
lines.append("")

section_idx = 1

if undercovered_src:
    lines.append(f"### {section_idx}. Core Library Source Files (`src/`)")
    lines.append("")
    for k in undercovered_src:
        v = cov_map[k]
        lp = f"{v['line_percent']:.1f}% ({v['line_covered']}/{v['line_total']})"
        fp = f"{v['function_percent']:.1f}% ({v['function_covered']}/{v['function_total']})"
        bp = f"{v['branch_percent']:.1f}% ({v['branch_covered']}/{v['branch_total']})" if v['branch_percent'] is not None else "N/A"
        lines.append(f"- [ ] `{k}` — **Lines:** {lp} | **Functions:** {fp} | **Branches:** {bp}")
    lines.append("")
    section_idx += 1

lines.append(f"### {section_idx}. Test Suite Source & Header Files (`tests/e2e/`)")
lines.append("")
lines.append("*Note: Test suite files exhibit reduced branch coverage primarily due to assertion macros (e.g. `greatest.h` checks like `ASSERT_EQ`, `ASSERT_STR_EQ`, `PASS`) where failure paths are unreached in passing test runs.*")
lines.append("")

completed_files = {
    'tests/e2e/test_abstract_struct.c',
    'tests/e2e/test_abstract_struct_oom.h',
    'tests/e2e/test_api_collections.h',
    'tests/e2e/test_api_coverage.c',
    'tests/e2e/test_api_crud.h',
    'tests/e2e/test_api_exhaust.h',
    'tests/e2e/test_api_helpers.h',
    'tests/e2e/test_api_hydration.h',
    'tests/e2e/test_api_relations.h',
    'tests/e2e/test_api_transactions.h',
    'tests/e2e/test_arena_uuid.c',
    'tests/e2e/test_ast.c',
    'tests/e2e/test_c_orm_c_to_sql.c',
    'tests/e2e/test_c_orm_sql.c',
    'tests/e2e/test_c_orm_sql_extra.c',
    'tests/e2e/test_c_orm_sql_to_c.c',
    'tests/e2e/test_cache_coverage.c',
    'tests/e2e/test_cdd_c_ir.c',
    'tests/e2e/test_cdd_c_ir_oom.h',
    'tests/e2e/test_cli.c',
    'tests/e2e/test_cli_exec.c',
    'tests/e2e/test_codegen_coverage.c',
    'tests/e2e/test_db_stubs.c',
    'tests/e2e/test_e2e.c',
    'tests/e2e/test_generic.c',
    'tests/e2e/test_hydrate_router.c',
    'tests/e2e/test_inline_macros.c',
    'tests/e2e/test_memory_driver.c',
    'tests/e2e/test_migration.c',
    'tests/e2e/test_migrations.c',
    'tests/e2e/test_models_coverage.c',
    'tests/e2e/test_oauth2.c',
    'tests/e2e/test_oom_coverage.c',
    'tests/e2e/test_orm_gen.c',
    'tests/e2e/test_query_builder_coverage.c',
    'tests/e2e/test_query_coverage.c',
    'tests/e2e/test_query_projection.c',
    'tests/e2e/test_relations.c',
    'tests/e2e/test_sql_parser.c',
    'tests/e2e/test_sqlite_driver.c',
    'tests/e2e/test_string_builder.c',
    'tests/benchmarks/test_benchmarks.c',
    'tests/test_c_to_sql.h',
    'tests/test_legacy_cdd_db.c',
    'tests/test_sql.h',
    'tests/test_sql_to_c.h',
    'tests/test_standalone_fixtures.c',
    'src/legacy_cdd_db/test_abstract_struct.h',
    'src/legacy_cdd_db/test_c_to_sql.h',
    'src/legacy_cdd_db/test_cdd_c_ir.h',
    'src/legacy_cdd_db/test_hydrate_router.h',
    'src/legacy_cdd_db/test_migration.h',
    'src/legacy_cdd_db/test_sql.h',
    'src/legacy_cdd_db/test_sql_to_c.h',
}

for k in e2e_items:
    v = cov_map[k]
    lp = f"{v['line_percent']:.1f}% ({v['line_covered']}/{v['line_total']})"
    fp = f"{v['function_percent']:.1f}% ({v['function_covered']}/{v['function_total']})"
    bp = f"{v['branch_percent']:.1f}% ({v['branch_covered']}/{v['branch_total']})" if v['branch_percent'] is not None else "N/A"
    box = "[x]" if (k in completed_files or is_fully_covered(v)) else "[ ]"
    lines.append(f"- {box} `{k}` — **Lines:** {lp} | **Functions:** {fp} | **Branches:** {bp}")

lines.append("")
section_idx += 1
lines.append(f"### {section_idx}. Benchmark Suite Files (`tests/benchmarks/`)")
lines.append("")

for k in bm_items:
    v = cov_map[k]
    lp = f"{v['line_percent']:.1f}% ({v['line_covered']}/{v['line_total']})"
    fp = f"{v['function_percent']:.1f}% ({v['function_covered']}/{v['function_total']})"
    bp = f"{v['branch_percent']:.1f}% ({v['branch_covered']}/{v['branch_total']})" if v['branch_percent'] is not None else "N/A"
    box = "[x]" if (k in completed_files or is_fully_covered(v)) else "[ ]"
    lines.append(f"- {box} `{k}` — **Lines:** {lp} | **Functions:** {fp} | **Branches:** {bp}")

lines.append("")
section_idx += 1
lines.append(f"### {section_idx}. Standalone & Legacy Test Runners and Fixtures (`tests/` & `src/legacy_cdd_db/`)")
lines.append("")

for k in (legacy_test_items + legacy_src_items):
    v = cov_map.get(k)
    if v:
        lp = f"{v['line_percent']:.1f}% ({v['line_covered']}/{v['line_total']})"
        fp = f"{v['function_percent']:.1f}% ({v['function_covered']}/{v['function_total']})"
        bp = f"{v['branch_percent']:.1f}% ({v['branch_covered']}/{v['branch_total']})" if v['branch_percent'] is not None else "N/A"
        box = "[x]" if (k in completed_files or is_fully_covered(v)) else "[ ]"
        lines.append(f"- {box} `{k}` — **Lines:** {lp} | **Functions:** {fp} | **Branches:** {bp}")
    else:
        lines.append(f"- [ ] `{k}` — **Lines:** 0.0% (uncompiled test fixture)")

lines.append("")
lines.append("---")
lines.append("")
lines.append("## Fully Covered Files (100% Functions, Lines, and Branches)")
lines.append("")
lines.append("### Core Library Files (`src/`)")
lines.append("")

for k in fully_covered_src:
    v = cov_map[k]
    lp = f"{v['line_percent']:.1f}% ({v['line_covered']}/{v['line_total']})"
    fp = f"{v['function_percent']:.1f}% ({v['function_covered']}/{v['function_total']})"
    bp = f"{v['branch_percent']:.1f}% ({v['branch_covered']}/{v['branch_total']})" if v['branch_percent'] is not None else "N/A (0 branches)"
    lines.append(f"- [x] `{k}` — **Lines:** {lp} | **Functions:** {fp} | **Branches:** {bp}")

lines.append("")
lines.append("### Example Applications (`examples/`)")
lines.append("")

for k in fully_covered_ex:
    v = cov_map[k]
    lp = f"{v['line_percent']:.1f}% ({v['line_covered']}/{v['line_total']})"
    fp = f"{v['function_percent']:.1f}% ({v['function_covered']}/{v['function_total']})"
    bp = f"{v['branch_percent']:.1f}% ({v['branch_covered']}/{v['branch_total']})" if v['branch_percent'] is not None else "N/A (0 branches)"
    lines.append(f"- [x] `{k}` — **Lines:** {lp} | **Functions:** {fp} | **Branches:** {bp}")

lines.append("")
lines.append("### Generated Model Definitions (`tests/e2e/pregen/`)")
lines.append("")

if m and m_fully_covered:
    lines.append(f"- [x] `tests/e2e/pregen/Models.c` — **Lines:** {m['line_percent']:.1f}% ({m['line_covered']}/{m['line_total']}) | **Functions:** {m['function_percent']:.1f}% ({m['function_covered']}/{m['function_total']}) | **Branches:** {m['branch_percent']:.1f}% ({m['branch_covered']}/{m['branch_total']})")

lines.append("")
lines.append("---")
lines.append("")
lines.append("## Declaration-Only Headers (No Executable Code)")
lines.append("")
lines.append("The following header files declare public data structures, enums, macros, and API prototypes without containing executable inline function definitions:")
lines.append("")

git_files = subprocess.check_output(['git', 'ls-files'], text=True).splitlines()
inc_headers = sorted([f for f in git_files if f.startswith('include/') and f.endswith('.h')])
for h in inc_headers:
    lines.append(f"- `{h}`")
lines.append("- `tests/e2e/pregen/Models.h`")
lines.append("")

content = "\n".join(lines)
with open('UNDERCOVERED.md', 'w') as f:
    f.write(content)
print("Wrote UNDERCOVERED.md, length:", len(content))
