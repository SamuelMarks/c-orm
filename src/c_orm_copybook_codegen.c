/**
 * @file c_orm_copybook_codegen.c
 * @brief Bidirectional COBOL Copybook (.cpy) <-> ISO C89 / SQL DDL / IBM Db2
 * DCLGEN code generation.
 */

/* clang-format off */
#include "c_orm_copybook.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_string_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Flattened column entry collected from a `c_orm_cobol_record_t` for ORM
 * metadata and SQL DDL emission.
 */
typedef struct cpy_flat_col {
  /** @brief Source AST field node. */
  const c_orm_cobol_field_t *field;
  /** @brief Effective C member path relative to `struct <Struct>` (e.g.
   * `"acct_bal"` or `"redef_u.acct_bal"`). */
  char c_member_path[128];
  /** @brief Effective C type string (e.g. `"c_orm_decimal_t"`, `"int32_t"`,
   * `"char"`). */
  const char *c_type_str;
  /** @brief Effective `c_orm_type_t` enum literal name. */
  const char *orm_type_name;
  /** @brief Non-zero when C field is a heap `char *` string pointer. */
  int is_heap_str;
  /** @brief Non-zero when C field is `c_orm_cobol_odo_array_t`. */
  int is_odo_array;
  /** @brief Inline fixed array capacity (`>0` for `char name[N+1]` or `unsigned
   * char name[N]`), else `0`. */
  size_t inline_fixed_len;
} cpy_flat_col_t;

/**
 * @brief Growable list of flattened columns for C/SQL codegen.
 */
typedef struct cpy_flat_vec {
  /** @brief Heap array of flattened columns. */
  cpy_flat_col_t *items;
  /** @brief Active item count. */
  size_t count;
  /** @brief Allocated item capacity. */
  size_t cap;
} cpy_flat_vec_t;

#define CPY_IS_DECIMAL_FIELD(f)                                                \
  (((f)->frac_digits > 0 || (f)->total_digits > 18 ||                          \
    (f)->usage == C_ORM_COBOL_USAGE_COMP_3)                                    \
       ? 1                                                                     \
       : 0)

#define CPY_IS_NATIONAL_FIELD(f)                                               \
  (((f)->pic_category == C_ORM_COBOL_PIC_NATIONAL ||                           \
    (f)->usage == C_ORM_COBOL_USAGE_DISPLAY_1)                                 \
       ? 1                                                                     \
       : 0)

/**
 * @brief Maps an elementary or DCLGEN COBOL field to its C89 type and
 * `c_orm_type_t` metadata representation.
 * @param f COBOL field node.
 * @param out_col Output populated type fields on `cpy_flat_col_t`.
 * @return 0.
 */
static int cpy_calc_classify_field(const c_orm_cobol_field_t *f,
                                   cpy_flat_col_t *out_col) {
  int is_dec = 0;
  int is_nat = 0;
  out_col->field = f;
  out_col->is_heap_str = 0;
  out_col->is_odo_array = 0;
  out_col->inline_fixed_len = 0;

  if (f->has_occurs && f->odo_field_name) {
    out_col->c_type_str = "c_orm_cobol_odo_array_t";
    out_col->orm_type_name = "C_ORM_TYPE_ARRAY";
    out_col->is_odo_array = 1;
    return 0;
  }
  if (f->is_dclgen_varchar) {
    out_col->c_type_str = "char *";
    out_col->orm_type_name =
        f->is_dclgen_vargraphic ? "C_ORM_TYPE_VARGRAPHIC" : "C_ORM_TYPE_STRING";
    out_col->is_heap_str = 1;
    return 0;
  }
  if (f->usage == C_ORM_COBOL_USAGE_COMP_1) {
    out_col->c_type_str = "float";
    out_col->orm_type_name = "C_ORM_TYPE_FLOAT";
    return 0;
  }
  if (f->usage == C_ORM_COBOL_USAGE_COMP_2) {
    out_col->c_type_str = "double";
    out_col->orm_type_name = "C_ORM_TYPE_DOUBLE";
    return 0;
  }
  if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
    is_dec = CPY_IS_DECIMAL_FIELD(f);
    if (is_dec) {
      out_col->c_type_str = "c_orm_decimal_t";
      out_col->orm_type_name = "C_ORM_TYPE_DECIMAL";
    } else if (f->total_digits <= 9) {
      out_col->c_type_str = "int32_t";
      out_col->orm_type_name = "C_ORM_TYPE_INT32";
    } else {
      out_col->c_type_str = "c_orm_int64_t";
      out_col->orm_type_name = "C_ORM_TYPE_INT64";
    }
    return 0;
  }
  if (f->sql_for_bit_data) {
    out_col->c_type_str = "unsigned char";
    out_col->orm_type_name = "C_ORM_TYPE_BINARY_FIXED";
    out_col->inline_fixed_len = f->element_byte_size;
    return 0;
  }
  is_nat = CPY_IS_NATIONAL_FIELD(f);
  if (is_nat) {
    out_col->c_type_str = "char *";
    out_col->orm_type_name = "C_ORM_TYPE_GRAPHIC";
    out_col->is_heap_str = 1;
    return 0;
  }
  out_col->c_type_str = "char";
  out_col->orm_type_name = "C_ORM_TYPE_CHAR_FIXED";
  out_col->inline_fixed_len = f->element_byte_size;
  return 0;
}

/**
 * @brief Appends a flattened column descriptor to `vec`.
 * @param vec Target vector.
 * @param item Column descriptor to copy.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_flat_vec_push(cpy_flat_vec_t *vec,
                                       const cpy_flat_col_t *item) {
  c_orm_error_t rc;
  if (vec->count == vec->cap) {
    size_t new_cap = (vec->cap == 0) ? 16 : (vec->cap * 2);
    void *new_mem = NULL;
    rc = c_orm_system_realloc(vec->items, new_cap * sizeof(cpy_flat_col_t),
                              &new_mem);
    if (rc != C_ORM_OK) {
      return rc;
    }
    vec->items = (cpy_flat_col_t *)new_mem;
    vec->cap = new_cap;
  }
  vec->items[vec->count++] = *item;
  return C_ORM_OK;
}

/**
 * @brief Checks if sibling `fields[idx]` is the base target of a subsequent
 * `REDEFINES` sibling at the same group level.
 * @param fields Array of sibling fields.
 * @param count Number of sibling fields.
 * @param idx Index to inspect.
 * @return 1 if redefined by a later sibling, 0 otherwise.
 */
static int cpy_cmp_is_redefined_base(const c_orm_cobol_field_t *fields,
                                     size_t count, size_t idx) {
  size_t j;
  for (j = idx + 1; j < count; ++j) {
    if (fields[j].redefines_target == &fields[idx]) {
      return 1;
    }
  }
  return 0;
}

/**
 * @brief Collects flattened primary ORM/SQL columns (excluding `FILLER`, level
 * `66`, and secondary `REDEFINES` overlays) while recording exact C struct
 * member paths (`<base>_u.<member>` when inside a `REDEFINES` union).
 * @param fields Array of sibling fields.
 * @param count Number of sibling fields.
 * @param prefix Current C member path prefix (empty `""` at root).
 * @param vec Output flattened column vector.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_collect_flat_cols(const c_orm_cobol_field_t *fields,
                                           size_t count, const char *prefix,
                                           cpy_flat_vec_t *vec) {
  size_t i;
  c_orm_error_t rc;

  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    int has_redef = 0;
    char next_prefix[128];

    if (f->level == 66 || f->is_filler || f->redefines_target != NULL) {
      continue;
    }
    has_redef = cpy_cmp_is_redefined_base(fields, count, i);

    if (has_redef) {
      if (prefix[0] != '\0') {
        rc = C_ORM_SPRINTF(next_prefix, sizeof(next_prefix), "%s.%s_u", prefix,
                           f->c_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        rc = C_ORM_SPRINTF(next_prefix, sizeof(next_prefix), "%s_u", f->c_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
    } else {
      rc = C_ORM_STRCPY(next_prefix, sizeof(next_prefix), prefix);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }

    if (f->is_dclgen_varchar) {
      cpy_flat_col_t fc;
      int cz;
      memset(&fc, 0, sizeof(fc));
      cz = cpy_calc_classify_field(f, &fc);
      fc.is_heap_str += (cz * 0);
      if (next_prefix[0] != '\0') {
        rc = C_ORM_SPRINTF(fc.c_member_path, sizeof(fc.c_member_path), "%s.%s",
                           next_prefix, f->c_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        rc =
            C_ORM_STRCPY(fc.c_member_path, sizeof(fc.c_member_path), f->c_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      rc = cpy_flat_vec_push(vec, &fc);
      if (rc != C_ORM_OK) {
        return rc;
      }
      continue;
    }

    if (f->num_children > 0) {
      if (f->has_occurs) {
        continue;
      }
      if (has_redef) {
        char grp_prefix[128];
        rc = C_ORM_SPRINTF(grp_prefix, sizeof(grp_prefix), "%s.%s", next_prefix,
                           f->c_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
        rc = cpy_collect_flat_cols(f->children, f->num_children, grp_prefix,
                                   vec);
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        rc = cpy_collect_flat_cols(f->children, f->num_children, next_prefix,
                                   vec);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      continue;
    }

    if (f->has_occurs && !f->odo_field_name) {
      continue;
    }

    {
      cpy_flat_col_t fc;
      int cz;
      memset(&fc, 0, sizeof(fc));
      cz = cpy_calc_classify_field(f, &fc);
      fc.is_heap_str += (cz * 0);
      if (next_prefix[0] != '\0') {
        rc = C_ORM_SPRINTF(fc.c_member_path, sizeof(fc.c_member_path), "%s.%s",
                           next_prefix, f->c_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        rc =
            C_ORM_STRCPY(fc.c_member_path, sizeof(fc.c_member_path), f->c_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      rc = cpy_flat_vec_push(vec, &fc);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Emits a single elementary/DCLGEN C89 struct member declaration with
 * Doxygen comment.
 * @param fp Output stream.
 * @param f COBOL field node.
 * @param indent Spaces indentation string.
 * @return 0.
 */
static int cpy_calc_emit_c_member(FILE *fp, const c_orm_cobol_field_t *f,
                                  const char *indent) {
  cpy_flat_col_t fc;
  int cz;
  memset(&fc, 0, sizeof(fc));

  fprintf(fp,
          "%s/** @brief COBOL `%s` (" C_ORM_FMT_SIZE_T
          " bytes at offset " C_ORM_FMT_SIZE_T "). */\n",
          indent, f->cobol_name, C_ORM_CAST_SIZE_T(f->total_byte_size),
          C_ORM_CAST_SIZE_T(f->byte_offset));

  cz = cpy_calc_classify_field(f, &fc);
  if (fc.is_odo_array) {
    fprintf(fp, "%sc_orm_cobol_odo_array_t %s;\n", indent, f->c_name);
    return cz;
  } else if (f->has_occurs) {
    if (fc.inline_fixed_len > 0) {
      size_t elem_cap =
          fc.inline_fixed_len + (strcmp(fc.c_type_str, "char") == 0 ? 1U : 0U);
      fprintf(fp, "%s%s %s[" C_ORM_FMT_SIZE_T "][" C_ORM_FMT_SIZE_T "];\n",
              indent, fc.c_type_str, f->c_name,
              C_ORM_CAST_SIZE_T(f->max_occurs), C_ORM_CAST_SIZE_T(elem_cap));
    } else {
      fprintf(fp, "%s%s %s[" C_ORM_FMT_SIZE_T "];\n", indent, fc.c_type_str,
              f->c_name, C_ORM_CAST_SIZE_T(f->max_occurs));
    }
  } else if (fc.inline_fixed_len > 0) {
    size_t cap =
        fc.inline_fixed_len + (strcmp(fc.c_type_str, "char") == 0 ? 1U : 0U);
    fprintf(fp, "%s%s %s[" C_ORM_FMT_SIZE_T "];\n", indent, fc.c_type_str,
            f->c_name, C_ORM_CAST_SIZE_T(cap));
  } else if (fc.is_heap_str) {
    fprintf(fp, "%schar *%s;\n", indent, f->c_name);
  } else {
    fprintf(fp, "%s%s %s;\n", indent, fc.c_type_str, f->c_name);
  }
  return cz;
}

/**
 * @brief Recursively emits pre-declared helper `struct` / `union` types for
 * `REDEFINES` branches so every C89 aggregate has a named, documented tag.
 * @param fp Output stream.
 * @param struct_name Top-level C struct name.
 * @param fields Sibling fields array.
 * @param count Number of sibling fields.
 * @return 0.
 */
static int cpy_calc_emit_redef_types(FILE *fp, const char *struct_name,
                                     const c_orm_cobol_field_t *fields,
                                     size_t count) {
  size_t i;
  int acc = 0;
  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    int has_redef = 0;

    if (f->level == 66 || f->is_filler) {
      continue;
    }
    if (f->num_children > 0 && !f->is_dclgen_varchar) {
      acc += cpy_calc_emit_redef_types(fp, struct_name, f->children,
                                       f->num_children);
    }
    if (f->redefines_target != NULL) {
      continue;
    }
    has_redef = cpy_cmp_is_redefined_base(fields, count, i);
    if (has_redef) {
      size_t j;
      for (j = i; j < count; ++j) {
        const c_orm_cobol_field_t *br = &fields[j];
        if (j != i && br->redefines_target != f) {
          continue;
        }
        if (br->num_children > 0 && !br->is_dclgen_varchar) {
          size_t ci;
          fprintf(fp, "/**\n * @brief Group branch `%s` inside `%s`.\n */\n",
                  br->cobol_name, struct_name);
          fprintf(fp, "struct %s_%s_grp {\n", struct_name, br->c_name);
          for (ci = 0; ci < br->num_children; ++ci) {
            if (!br->children[ci].is_filler &&
                br->children[ci].num_children == 0) {
              acc += cpy_calc_emit_c_member(fp, &br->children[ci], "  ");
            }
          }
          fprintf(fp, "};\n\n");
        }
      }
      fprintf(fp,
              "/**\n * @brief C89 `REDEFINES` union overlaying `%s`.\n */\n",
              f->cobol_name);
      fprintf(fp, "union %s_%s_redef {\n", struct_name, f->c_name);
      for (j = i; j < count; ++j) {
        const c_orm_cobol_field_t *br = &fields[j];
        if (j != i && br->redefines_target != f) {
          continue;
        }
        if (br->num_children > 0 && !br->is_dclgen_varchar) {
          fprintf(fp, "  /** @brief Group overlay `%s`. */\n", br->cobol_name);
          fprintf(fp, "  struct %s_%s_grp %s;\n", struct_name, br->c_name,
                  br->c_name);
        } else {
          acc += cpy_calc_emit_c_member(fp, br, "  ");
        }
      }
      fprintf(fp, "};\n\n");
    }
  }
  return acc;
}

/**
 * @brief Emits struct body members for `fields` inside `struct <struct_name>`.
 * @param fp Output stream.
 * @param struct_name Top-level C struct name.
 * @param fields Sibling fields.
 * @param count Number of sibling fields.
 * @return 0.
 */
static int cpy_calc_emit_struct_members(FILE *fp, const char *struct_name,
                                        const c_orm_cobol_field_t *fields,
                                        size_t count) {
  size_t i;
  int acc = 0;
  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    int has_redef = 0;

    if (f->level == 66 || f->is_filler || f->redefines_target != NULL) {
      continue;
    }
    has_redef = cpy_cmp_is_redefined_base(fields, count, i);
    if (has_redef) {
      fprintf(fp, "  /** @brief `REDEFINES` overlay union for `%s`. */\n",
              f->cobol_name);
      fprintf(fp, "  union %s_%s_redef %s_u;\n", struct_name, f->c_name,
              f->c_name);
      continue;
    }
    if (f->is_dclgen_varchar || f->num_children == 0) {
      acc += cpy_calc_emit_c_member(fp, f, "  ");
    } else if (!f->has_occurs) {
      acc += cpy_calc_emit_struct_members(fp, struct_name, f->children,
                                          f->num_children);
    }
  }
  return acc;
}

/**
 * @brief Emits Doxygen-documented level-`88` predicate prototypes for all
 * fields in `fields`.
 * @param fp Output stream.
 * @param struct_name Top-level C struct name.
 * @param fields Sibling fields.
 * @param count Number of sibling fields.
 * @return 0.
 */
static int cpy_calc_emit_88_protos(FILE *fp, const char *struct_name,
                                   const c_orm_cobol_field_t *fields,
                                   size_t count) {
  size_t i;
  int acc = 0;
  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    size_t ci;
    if (!f->is_filler && f->redefines_target == NULL) {
      for (ci = 0; ci < f->num_conditions; ++ci) {
        const c_orm_cobol_condition_t *cnd = &f->conditions[ci];
        fprintf(fp, "/**\n");
        fprintf(fp,
                " * @brief Evaluates COBOL level-88 condition `%s` on `%s`.\n",
                cnd->cobol_name, f->cobol_name);
        fprintf(fp, " * @param item Pointer to `%s` struct.\n", struct_name);
        fprintf(fp, " * @param out_matched Output flag set to 1 if matched.\n");
        fprintf(fp, " * @return C_ORM_OK on success.\n");
        fprintf(fp, " */\n");
        fprintf(fp,
                "c_orm_error_t %s_is_%s(const struct %s *item, int "
                "*out_matched);\n\n",
                struct_name, cnd->c_name, struct_name);
      }
    }
    if (f->num_children > 0 && !f->is_dclgen_varchar) {
      acc += cpy_calc_emit_88_protos(fp, struct_name, f->children,
                                     f->num_children);
    }
  }
  return acc;
}

C_ORM_EXPORT c_orm_error_t
c_orm_copybook_emit_c_header_body(FILE *fp, const c_orm_cobol_record_t *rec) {
  const char *sn;
  int acc = 0;
  if (!fp || !rec || !rec->c_struct_name) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  sn = rec->c_struct_name;

  acc += cpy_calc_emit_redef_types(fp, sn, rec->fields, rec->num_fields);

  fprintf(fp, "/**\n");
  fprintf(
      fp,
      " * @brief Generated C89 model for COBOL Copybook `%s` (" C_ORM_FMT_SIZE_T
      " wire bytes).\n",
      rec->record_name, C_ORM_CAST_SIZE_T(rec->total_byte_size));
  fprintf(fp, " */\n");
  fprintf(fp, "struct %s {\n", sn);
  acc += cpy_calc_emit_struct_members(fp, sn, rec->fields, rec->num_fields);
  if (rec->num_fields == 0) {
    fprintf(fp, "  /** @brief Placeholder byte for standalone record. */\n");
    fprintf(fp, "  unsigned char _placeholder;\n");
  }
  fprintf(fp, "};\n\n");

  fprintf(fp, "/**\n * @brief Dynamic array container for `%s` rows.\n */\n",
          sn);
  fprintf(fp, "struct %s_Array {\n", sn);
  fprintf(fp, "  /** @brief Contiguous array of `%s` items. */\n", sn);
  fprintf(fp, "  struct %s *data;\n", sn);
  fprintf(fp, "  /** @brief Active item count. */\n");
  fprintf(fp, "  size_t length;\n");
  fprintf(fp, "  /** @brief Allocated item capacity. */\n");
  fprintf(fp, "  size_t capacity;\n");
  fprintf(fp, "};\n\n");

  fprintf(fp, "/** @brief `c-orm` table metadata descriptor for `%s`. */\n",
          sn);
  fprintf(fp, "extern const c_orm_table_meta_t %s_meta;\n\n", sn);

  fprintf(fp,
          "/**\n * @brief Initializes a `%s` struct to zero/defaults.\n"
          " * @param item Target struct.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_init(struct %s *item);\n\n", sn, sn);

  fprintf(fp,
          "/**\n * @brief Releases dynamic fields inside `%s`.\n"
          " * @param item Target struct.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_free(struct %s *item);\n\n", sn, sn);

  fprintf(fp,
          "/**\n * @brief Deep-copies `%s` `src` into `dest`.\n"
          " * @param src Source struct.\n"
          " * @param dest Destination struct.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp,
          "c_orm_error_t %s_deepcopy(const struct %s *src, struct %s "
          "*dest);\n\n",
          sn, sn, sn);

  fprintf(fp,
          "/**\n * @brief Initializes a `%s_Array` container.\n"
          " * @param arr Target array.\n"
          " * @param initial_capacity Initial slot count.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp,
          "c_orm_error_t %s_Array_init(struct %s_Array *arr, size_t "
          "initial_capacity);\n\n",
          sn, sn);

  fprintf(fp,
          "/**\n * @brief Frees a `%s_Array` container.\n"
          " * @param arr Target array.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_Array_free(struct %s_Array *arr);\n\n", sn, sn);

  fprintf(fp,
          "/**\n * @brief Validates `%s` column precision and level-88 "
          "domains.\n"
          " * @param item Struct to validate.\n"
          " * @return C_ORM_OK when valid.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_validate(const struct %s *item);\n\n", sn, sn);

  fprintf(fp,
          "/**\n * @brief Packs `%s` into a binary COBOL mainframe wire "
          "record.\n"
          " * @param item Source struct.\n"
          " * @param cp Target codepage.\n"
          " * @param big_endian_comp Non-zero for big-endian `COMP`.\n"
          " * @param out_buf Destination wire buffer.\n"
          " * @param buf_cap Capacity of `out_buf`.\n"
          " * @param out_len Optional packed byte length.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp,
          "c_orm_error_t %s_pack(const struct %s *item, c_orm_codepage_t cp, "
          "int big_endian_comp, unsigned char *out_buf, size_t buf_cap, size_t "
          "*out_len);\n\n",
          sn, sn);

  fprintf(fp,
          "/**\n * @brief Unpacks a binary COBOL mainframe wire record into "
          "`%s`.\n"
          " * @param raw_buf Source wire bytes.\n"
          " * @param raw_len Length of `raw_buf`.\n"
          " * @param cp Source codepage.\n"
          " * @param big_endian_comp Non-zero for big-endian `COMP`.\n"
          " * @param out_item Destination struct.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp,
          "c_orm_error_t %s_unpack(const unsigned char *raw_buf, size_t "
          "raw_len, c_orm_codepage_t cp, int big_endian_comp, struct %s "
          "*out_item);\n\n",
          sn, sn);

  acc += cpy_calc_emit_88_protos(fp, sn, rec->fields, rec->num_fields);
  return (c_orm_error_t)(acc * 0);
}

/**
 * @brief Branch-free wrapper invoking `c_orm_copybook_emit_c_header_body` for
 * validated non-NULL `fp` and `rec`.
 * @param fp Output stream.
 * @param rec Validated COBOL record.
 * @return Status code cast to int.
 */
static int cpy_calc_emit_hdr_body_checked(FILE *fp,
                                          const c_orm_cobol_record_t *rec) {
  return (int)c_orm_copybook_emit_c_header_body(fp, rec);
}

C_ORM_EXPORT c_orm_error_t
c_orm_copybook_emit_c_header(FILE *fp, const c_orm_cobol_record_t *rec) {
  int st;
  if (!fp || !rec || !rec->c_struct_name) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  fprintf(fp, "/**\n * @file Models.h\n * @brief Generated ISO C89 COBOL "
              "Copybook ORM models.\n */\n\n");
  fprintf(fp, "#ifndef C_ORM_GENERATED_COPYBOOK_%s_H\n", rec->sql_table_name);
  fprintf(fp, "#define C_ORM_GENERATED_COPYBOOK_%s_H\n\n", rec->sql_table_name);
  fprintf(fp, "/* clang-format "
              "off */\n");
  fprintf(fp, "#include \"c_orm_copybook.h\"\n");
  fprintf(fp, "#include \"c_orm_decimal.h\"\n");
  fprintf(fp, "#include \"c_orm_ebcdic.h\"\n");
  fprintf(fp, "#include \"c_orm_meta.h\"\n");
  fprintf(fp, "#include <stddef.h>\n");
  fprintf(fp, "/* clang-format "
              "on */\n\n");
  fprintf(fp,
          "#ifdef __cplusplus\nextern \"C\" {\n#endif /* __cplusplus */\n\n");
  st = cpy_calc_emit_hdr_body_checked(fp, rec);
  fprintf(fp, "#ifdef __cplusplus\n}\n#endif /* __cplusplus */\n\n");
  fprintf(fp, "#endif /* C_ORM_GENERATED_COPYBOOK_%s_H */\n",
          rec->sql_table_name);
  return (c_orm_error_t)(st * 0);
}

/**
 * @brief Formats `prefix + mid + suffix` into a stack buffer via Safe CRT and
 * appends it to `sb`.
 * @param sb Target string builder.
 * @param prefix Leading literal string.
 * @param mid Middle dynamic string.
 * @param suffix Trailing literal string.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t cpy_sb_append_kv(c_orm_string_builder_t *sb,
                                      const char *prefix, const char *mid,
                                      const char *suffix) {
  char buf[256];
  c_orm_error_t rc;
  rc = C_ORM_SPRINTF(buf, sizeof(buf), "%s%s%s", prefix, mid, suffix);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return c_orm_string_builder_append(sb, buf);
}

/**
 * @brief Reconstructs a normalized free-format COBOL Copybook text from `rec`
 * so generated `<Struct>_pack` / `<Struct>_unpack` functions operate with full
 * group, `REDEFINES`, `COMP-3`, and `ODO` fidelity.
 * @param sb Target string builder.
 * @param fields Sibling fields.
 * @param count Number of sibling fields.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_reconstruct_fields(c_orm_string_builder_t *sb,
                                            const c_orm_cobol_field_t *fields,
                                            size_t count) {
  size_t i;
  char tmp[256];
  c_orm_error_t rc;

  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    const char *usage_s = "";
    const char *sign_s = "";
    size_t ci;
    if (f->level == 66) {
      continue;
    }
    switch (f->usage) {
    case C_ORM_COBOL_USAGE_COMP:
      usage_s = " USAGE COMP";
      break;
    case C_ORM_COBOL_USAGE_COMP_5:
      usage_s = " USAGE COMP-5";
      break;
    case C_ORM_COBOL_USAGE_COMP_1:
      usage_s = " USAGE COMP-1";
      break;
    case C_ORM_COBOL_USAGE_COMP_2:
      usage_s = " USAGE COMP-2";
      break;
    case C_ORM_COBOL_USAGE_COMP_3:
      usage_s = " USAGE COMP-3";
      break;
    case C_ORM_COBOL_USAGE_DISPLAY_1:
      usage_s = " USAGE DISPLAY-1";
      break;
    default:
      break;
    }
    if (f->is_signed && (f->sign_leading || f->sign_separate)) {
      sign_s = f->sign_leading ? (f->sign_separate ? " SIGN LEADING SEPARATE"
                                                   : " SIGN LEADING")
                               : " SIGN TRAILING SEPARATE";
    }
    rc = C_ORM_SPRINTF(tmp, sizeof(tmp), "  %02d %s", f->level,
                       f->is_filler ? "FILLER" : f->cobol_name);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc = cpy_sb_append_kv(sb, tmp, usage_s, sign_s);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (f->redefines_name) {
      rc = cpy_sb_append_kv(sb, " REDEFINES ", f->redefines_name, "");
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (f->pic_raw) {
      rc = cpy_sb_append_kv(sb, " PIC ", f->pic_raw, "");
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (f->has_occurs) {
      if (f->odo_field_name) {
        rc = C_ORM_SPRINTF(tmp, sizeof(tmp),
                           " OCCURS " C_ORM_FMT_SIZE_T " TO " C_ORM_FMT_SIZE_T
                           " DEPENDING ON %s.\n",
                           C_ORM_CAST_SIZE_T(f->min_occurs),
                           C_ORM_CAST_SIZE_T(f->max_occurs), f->odo_field_name);
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        rc = C_ORM_SPRINTF(tmp, sizeof(tmp),
                           " OCCURS " C_ORM_FMT_SIZE_T " TIMES.\n",
                           C_ORM_CAST_SIZE_T(f->max_occurs));
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      rc = c_orm_string_builder_append(sb, tmp);
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else {
      rc = c_orm_string_builder_append(sb, ".\n");
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    for (ci = 0; ci < f->num_conditions; ++ci) {
      const c_orm_cobol_condition_t *cnd = &f->conditions[ci];
      size_t vi;
      rc = cpy_sb_append_kv(sb, "    88 ", cnd->cobol_name, " VALUE");
      if (rc != C_ORM_OK) {
        return rc;
      }
      for (vi = 0; vi < cnd->num_values; ++vi) {
        const c_orm_cobol_value_range_t *vr = &cnd->values[vi];
        const char *end_s = (vi + 1 == cnd->num_values) ? ".\n" : "";
        if (vr->is_range) {
          rc = cpy_sb_append_kv(sb, vr->is_numeric ? " " : " '", vr->low_val,
                                vr->is_numeric ? "" : "'");
          if (rc != C_ORM_OK) {
            return rc;
          }
          rc = cpy_sb_append_kv(
              sb, vr->is_numeric ? " THRU " : " THRU '", vr->high_val,
              vr->is_numeric ? end_s
                             : ((vi + 1 == cnd->num_values) ? "'.\n" : "'"));
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else {
          rc = cpy_sb_append_kv(
              sb, vr->is_numeric ? " " : " '", vr->low_val,
              vr->is_numeric ? end_s
                             : ((vi + 1 == cnd->num_values) ? "'.\n" : "'"));
          if (rc != C_ORM_OK) {
            return rc;
          }
        }
      }
    }
    if (f->num_children > 0) {
      rc = cpy_reconstruct_fields(sb, f->children, f->num_children);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Emits a C89 `static const char` array initializer (`{ 0x..., 0x00 }`)
 * avoiding the ISO C90 509-byte string-literal length limit.
 * @param fp Output file stream.
 * @param s Source null-terminated string.
 * @return 0.
 */
static int cpy_calc_emit_char_array(FILE *fp, const char *s) {
  size_t i;
  size_t len = strlen(s);
  fprintf(fp, "{\n  ");
  for (i = 0; i <= len; ++i) {
    unsigned int byte_val = (unsigned int)((unsigned char)s[i]);
    fprintf(fp, "(char)0x%02xU%s", byte_val, (i == len) ? "" : ", ");
    if (i < len && ((i + 1) % 8U) == 0U) {
      fprintf(fp, "\n  ");
    }
  }
  fprintf(fp, "\n}");
  return 0;
}

/**
 * @brief Emits level-`88` predicate implementations for `fields`.
 * @param fp Output stream.
 * @param struct_name C struct name.
 * @param fields Sibling fields.
 * @param count Number of sibling fields.
 * @return 0.
 */
static int cpy_calc_emit_88_impls(FILE *fp, const char *struct_name,
                                  const c_orm_cobol_field_t *fields,
                                  size_t count) {
  size_t i;
  int acc = 0;
  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    size_t ci;
    if (!f->is_filler && f->redefines_target == NULL) {
      for (ci = 0; ci < f->num_conditions; ++ci) {
        const c_orm_cobol_condition_t *cnd = &f->conditions[ci];
        fprintf(
            fp,
            "/**\n * @brief Evaluates COBOL level-88 condition `%s` on `%s`.\n"
            " * @param item Pointer to `%s` struct.\n"
            " * @param out_matched Output flag set to 1 if matched.\n"
            " * @return C_ORM_OK on success.\n */\n",
            cnd->cobol_name, f->cobol_name, struct_name);
        fprintf(fp,
                "c_orm_error_t %s_is_%s(const struct %s *item, int "
                "*out_matched) {\n",
                struct_name, cnd->c_name, struct_name);
        fprintf(fp, "  c_orm_cobol_record_t *rec = NULL;\n");
        fprintf(fp, "  c_orm_error_t rc;\n");
        fprintf(fp, "  if (!item || !out_matched) {\n    return "
                    "C_ORM_ERROR_NULL_POINTER;\n  }\n");
        fprintf(fp,
                "  rc = c_orm_copybook_parse_string(%s_cpy_spec, "
                "C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec);\n",
                struct_name);
        fprintf(fp, "  if (rc != C_ORM_OK) {\n    return rc;\n  }\n");
        fprintf(fp,
                "  rc = c_orm_copybook_check_condition(rec, &%s_meta, item, "
                "\"%s\", out_matched);\n",
                struct_name, cnd->cobol_name);
        fprintf(fp, "  c_orm_copybook_record_free(rec);\n");
        fprintf(fp, "  return rc;\n}\n\n");
      }
    }
    if (f->num_children > 0 && !f->is_dclgen_varchar) {
      acc +=
          cpy_calc_emit_88_impls(fp, struct_name, f->children, f->num_children);
    }
  }
  return acc;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_emit_c_source(
    FILE *fp, const c_orm_cobol_record_t *rec, const char *header_name) {
  cpy_flat_vec_t cols;
  c_orm_string_builder_t *spec_sb = NULL;
  const char *spec_str = NULL;
  const char *sn;
  const char *pk_name = NULL;
  size_t pk_count = 0;
  size_t i;
  c_orm_error_t rc;

  if (!fp || !rec || !rec->c_struct_name || !header_name) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  sn = rec->c_struct_name;
  memset(&cols, 0, sizeof(cols));

  rc = cpy_collect_flat_cols(rec->fields, rec->num_fields, "", &cols);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_string_builder_init(&spec_sb);
  if (rc != C_ORM_OK) {
    c_orm_system_free(cols.items);
    return rc;
  }
  rc = c_orm_string_builder_append(spec_sb, ">>SOURCE FORMAT FREE\n01 ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(spec_sb);
    c_orm_system_free(cols.items);
    return rc;
  }
  rc = c_orm_string_builder_append(spec_sb, rec->record_name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(spec_sb);
    c_orm_system_free(cols.items);
    return rc;
  }
  rc = c_orm_string_builder_append(spec_sb, ".\n");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(spec_sb);
    c_orm_system_free(cols.items);
    return rc;
  }
  rc = cpy_reconstruct_fields(spec_sb, rec->fields, rec->num_fields);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(spec_sb);
    c_orm_system_free(cols.items);
    return rc;
  }
  rc = c_orm_string_builder_get(spec_sb, &spec_str);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(spec_sb);
    c_orm_system_free(cols.items);
    return rc;
  }

  fprintf(fp, "/**\n * @file Models.c\n * @brief Generated ISO C89 COBOL "
              "Copybook ORM implementation.\n */\n\n");
  fprintf(fp, "/* clang-format "
              "off */\n");
  fprintf(fp, "#include \"%s\"\n", header_name);
  fprintf(fp, "#include \"c_orm_api.h\"\n");
  fprintf(fp, "#include \"c_orm_safe_crt.h\"\n");
  fprintf(fp, "#include <stddef.h>\n");
  fprintf(fp, "#include <stdlib.h>\n");
  fprintf(fp, "#include <string.h>\n");
  fprintf(fp, "/* clang-format "
              "on */\n\n");

  fprintf(fp,
          "/** @brief Embedded normalized COBOL Copybook spec for `%s`. */\n",
          sn);
  fprintf(fp, "static const char %s_cpy_spec[] = ", sn);
  pk_count += (size_t)(cpy_calc_emit_char_array(fp, spec_str) * 0);
  fprintf(fp, ";\n\n");
  c_orm_string_builder_free(spec_sb);

  /* init */
  fprintf(fp,
          "/**\n * @brief Initializes a `%s` struct to zero/defaults.\n"
          " * @param item Target struct.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_init(struct %s *item) {\n", sn, sn);
  fprintf(fp, "  if (!item) {\n    return C_ORM_ERROR_NULL_POINTER;\n  }\n");
  fprintf(fp, "  memset(item, 0, sizeof(*item));\n");
  fprintf(fp, "  return C_ORM_OK;\n}\n\n");

  /* free */
  fprintf(fp,
          "/**\n * @brief Releases dynamic fields inside `%s`.\n"
          " * @param item Target struct.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_free(struct %s *item) {\n", sn, sn);
  fprintf(fp, "  if (!item) {\n    return C_ORM_OK;\n  }\n");
  for (i = 0; i < cols.count; ++i) {
    if (cols.items[i].is_heap_str) {
      fprintf(fp,
              "  if (item->%s) {\n    c_orm_system_free(item->%s);\n"
              "    item->%s = NULL;\n  }\n",
              cols.items[i].c_member_path, cols.items[i].c_member_path,
              cols.items[i].c_member_path);
    } else if (cols.items[i].is_odo_array) {
      fprintf(fp,
              "  if (item->%s.data) {\n"
              "    c_orm_system_free(item->%s.data);\n"
              "    item->%s.data = NULL;\n"
              "    item->%s.length = 0;\n"
              "    item->%s.capacity = 0;\n  }\n",
              cols.items[i].c_member_path, cols.items[i].c_member_path,
              cols.items[i].c_member_path, cols.items[i].c_member_path,
              cols.items[i].c_member_path);
    }
  }
  fprintf(fp, "  return C_ORM_OK;\n}\n\n");

  /* deepcopy */
  fprintf(fp, "/**\n * @brief Deep-copies `src` into `dest`.\n"
              " * @param src Source struct.\n"
              " * @param dest Destination struct.\n"
              " * @return C_ORM_OK on success.\n */\n");
  fprintf(fp,
          "c_orm_error_t %s_deepcopy(const struct %s *src, struct %s *dest) "
          "{\n",
          sn, sn, sn);
  fprintf(
      fp,
      "  if (!src || !dest) {\n    return C_ORM_ERROR_NULL_POINTER;\n  }\n");
  fprintf(fp, "  *dest = *src;\n");
  for (i = 0; i < cols.count; ++i) {
    if (cols.items[i].is_heap_str) {
      fprintf(fp, "  dest->%s = NULL;\n", cols.items[i].c_member_path);
    } else if (cols.items[i].is_odo_array) {
      fprintf(fp, "  dest->%s.data = NULL;\n", cols.items[i].c_member_path);
    }
  }
  for (i = 0; i < cols.count; ++i) {
    if (cols.items[i].is_heap_str) {
      fprintf(fp,
              "  if (src->%s) {\n"
              "    if (c_orm_strdup(src->%s, &dest->%s) != C_ORM_OK) {\n"
              "      %s_free(dest);\n"
              "      return C_ORM_ERROR_MEMORY;\n    }\n  }\n",
              cols.items[i].c_member_path, cols.items[i].c_member_path,
              cols.items[i].c_member_path, sn);
    }
  }
  fprintf(fp, "  return C_ORM_OK;\n}\n\n");

  /* Array_init & Array_free */
  fprintf(fp,
          "/**\n * @brief Initializes a `%s_Array` container.\n"
          " * @param arr Target array.\n"
          " * @param initial_capacity Initial slot count.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp,
          "c_orm_error_t %s_Array_init(struct %s_Array *arr, size_t "
          "initial_capacity) {\n",
          sn, sn);
  fprintf(fp, "  if (!arr) {\n    return C_ORM_ERROR_NULL_POINTER;\n  }\n");
  fprintf(fp, "  arr->length = 0;\n  arr->capacity = initial_capacity;\n");
  fprintf(fp, "  if (initial_capacity > 0) {\n");
  fprintf(fp,
          "    if (c_orm_system_calloc(initial_capacity, sizeof(struct %s), "
          "(void **)&arr->data) != C_ORM_OK) {\n      return "
          "C_ORM_ERROR_MEMORY;\n    }\n",
          sn);
  fprintf(fp, "  } else {\n    arr->data = NULL;\n  }\n");
  fprintf(fp, "  return C_ORM_OK;\n}\n\n");

  fprintf(fp,
          "/**\n * @brief Frees a `%s_Array` container.\n"
          " * @param arr Target array.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_Array_free(struct %s_Array *arr) {\n", sn, sn);
  fprintf(fp, "  size_t i;\n  if (!arr) {\n    return C_ORM_OK;\n  }\n");
  fprintf(fp,
          "  if (arr->data) {\n    for (i = 0; i < arr->length; ++i) {\n"
          "      %s_free(&arr->data[i]);\n    }\n"
          "    c_orm_system_free(arr->data);\n    arr->data = NULL;\n"
          "  }\n  arr->length = 0;\n  arr->capacity = 0;\n"
          "  return C_ORM_OK;\n}\n\n",
          sn);

  /* validate, pack, unpack */
  fprintf(fp,
          "/**\n * @brief Validates `%s` column precision and level-88 "
          "domains.\n"
          " * @param item Struct to validate.\n"
          " * @return C_ORM_OK when valid.\n */\n",
          sn);
  fprintf(fp, "c_orm_error_t %s_validate(const struct %s *item) {\n", sn, sn);
  fprintf(fp, "  c_orm_cobol_record_t *rec = NULL;\n  c_orm_error_t rc;\n");
  fprintf(fp, "  if (!item) {\n    return C_ORM_ERROR_NULL_POINTER;\n  }\n");
  fprintf(fp, "  rc = c_orm_validate(&%s_meta, item);\n", sn);
  fprintf(fp, "  if (rc != C_ORM_OK) {\n    return rc;\n  }\n");
  fprintf(fp,
          "  rc = c_orm_copybook_parse_string(%s_cpy_spec, "
          "C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec);\n",
          sn);
  fprintf(fp, "  if (rc != C_ORM_OK) {\n    return rc;\n  }\n");
  fprintf(fp, "  rc = c_orm_copybook_validate_88(rec, &%s_meta, item);\n", sn);
  fprintf(fp, "  c_orm_copybook_record_free(rec);\n");
  fprintf(fp, "  return rc;\n}\n\n");

  fprintf(fp,
          "/**\n * @brief Packs `%s` into a binary COBOL mainframe wire "
          "record.\n"
          " * @param item Source struct.\n"
          " * @param cp Target codepage.\n"
          " * @param big_endian_comp Non-zero for big-endian `COMP`.\n"
          " * @param out_buf Destination wire buffer.\n"
          " * @param buf_cap Capacity of `out_buf`.\n"
          " * @param out_len Optional packed byte length.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp,
          "c_orm_error_t %s_pack(const struct %s *item, c_orm_codepage_t cp, "
          "int big_endian_comp, unsigned char *out_buf, size_t buf_cap, size_t "
          "*out_len) {\n",
          sn, sn);
  fprintf(fp, "  c_orm_cobol_record_t *rec = NULL;\n  c_orm_error_t rc;\n");
  fprintf(fp, "  if (!item || !out_buf) {\n    return "
              "C_ORM_ERROR_NULL_POINTER;\n  }\n");
  fprintf(fp,
          "  rc = c_orm_copybook_parse_string(%s_cpy_spec, "
          "C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec);\n",
          sn);
  fprintf(fp, "  if (rc != C_ORM_OK) {\n    return rc;\n  }\n");
  fprintf(fp,
          "  rc = c_orm_copybook_pack_struct(rec, &%s_meta, item, cp, "
          "big_endian_comp, out_buf, buf_cap, out_len);\n",
          sn);
  fprintf(fp, "  c_orm_copybook_record_free(rec);\n");
  fprintf(fp, "  return rc;\n}\n\n");

  fprintf(fp,
          "/**\n * @brief Unpacks a binary COBOL mainframe wire record into "
          "`%s`.\n"
          " * @param raw_buf Source wire bytes.\n"
          " * @param raw_len Length of `raw_buf`.\n"
          " * @param cp Source codepage.\n"
          " * @param big_endian_comp Non-zero for big-endian `COMP`.\n"
          " * @param out_item Destination struct.\n"
          " * @return C_ORM_OK on success.\n */\n",
          sn);
  fprintf(fp,
          "c_orm_error_t %s_unpack(const unsigned char *raw_buf, size_t "
          "raw_len, c_orm_codepage_t cp, int big_endian_comp, struct %s "
          "*out_item) {\n",
          sn, sn);
  fprintf(fp, "  c_orm_cobol_record_t *rec = NULL;\n  c_orm_error_t rc;\n");
  fprintf(fp, "  if (!raw_buf || !out_item) {\n    return "
              "C_ORM_ERROR_NULL_POINTER;\n  }\n");
  fprintf(fp,
          "  rc = c_orm_copybook_parse_string(%s_cpy_spec, "
          "C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec);\n",
          sn);
  fprintf(fp, "  if (rc != C_ORM_OK) {\n    return rc;\n  }\n");
  fprintf(fp,
          "  rc = c_orm_copybook_unpack_struct(rec, &%s_meta, raw_buf, "
          "raw_len, cp, big_endian_comp, out_item);\n",
          sn);
  fprintf(fp, "  c_orm_copybook_record_free(rec);\n");
  fprintf(fp, "  return rc;\n}\n\n");

  pk_count +=
      (size_t)(cpy_calc_emit_88_impls(fp, sn, rec->fields, rec->num_fields) *
               0);

  /* Metadata queries & descriptors */
  for (i = 0; i < cols.count; ++i) {
    if (cols.items[i].field->sql_is_pk) {
      pk_count++;
      pk_name = cols.items[i].field->c_name;
    }
  }
  if (pk_count == 0 && cols.count > 0 && !cols.items[0].is_odo_array) {
    pk_count = 1;
    pk_name = cols.items[0].field->c_name;
  }

  fprintf(fp, "/** @brief Default SELECT ALL SQL for `%s`. */\n", sn);
  fprintf(fp, "#define %s_query_select_all \"SELECT * FROM %s\"\n", sn,
          rec->sql_table_name);
  if (pk_count == 1) {
    fprintf(fp, "/** @brief Default SELECT BY PK SQL for `%s`. */\n", sn);
    fprintf(fp,
            "#define %s_query_select_by_pk \"SELECT * FROM %s WHERE %s = ?\"\n",
            sn, rec->sql_table_name, pk_name);
    fprintf(fp, "/** @brief Default DELETE BY PK SQL for `%s`. */\n", sn);
    fprintf(fp,
            "#define %s_query_delete_by_pk \"DELETE FROM %s WHERE %s = ?\"\n",
            sn, rec->sql_table_name, pk_name);
  } else {
    fprintf(fp, "/** @brief Default SELECT BY PK SQL for `%s`. */\n", sn);
    fprintf(fp, "#define %s_query_select_by_pk NULL\n", sn);
    fprintf(fp, "/** @brief Default DELETE BY PK SQL for `%s`. */\n", sn);
    fprintf(fp, "#define %s_query_delete_by_pk NULL\n", sn);
  }

  fprintf(fp, "/** @brief Default INSERT SQL for `%s`. */\n", sn);
  fprintf(fp, "#define %s_query_insert \"INSERT INTO %s (", sn,
          rec->sql_table_name);
  for (i = 0; i < cols.count; ++i) {
    fprintf(fp, "%s%s", cols.items[i].field->c_name,
            (i + 1 == cols.count) ? "" : ", ");
  }
  fprintf(fp, ") VALUES (");
  for (i = 0; i < cols.count; ++i) {
    fprintf(fp, "?%s", (i + 1 == cols.count) ? "" : ", ");
  }
  fprintf(fp, ")\"\n");

  fprintf(fp, "/** @brief Default UPDATE SQL for `%s`. */\n", sn);
  fprintf(fp, "#define %s_query_update \"UPDATE %s SET ", sn,
          rec->sql_table_name);
  for (i = 0; i < cols.count; ++i) {
    fprintf(fp, "%s = ?%s", cols.items[i].field->c_name,
            (i + 1 == cols.count) ? "" : ", ");
  }
  if (pk_count == 1) {
    fprintf(fp, " WHERE %s = ?\"\n\n", pk_name);
  } else {
    fprintf(fp, "\"\n\n");
  }

  fprintf(fp, "/** @brief Column reflection descriptors for `%s`. */\n", sn);
  fprintf(fp, "static const c_orm_column_meta_t %s_meta_columns[] = {\n", sn);
  if (cols.count == 0) {
    fprintf(fp, "  { \"_placeholder\", C_ORM_TYPE_INT32, 0, false, true, NULL, "
                "false, false, 0, 0, 0, 0 }\n");
  }
  for (i = 0; i < cols.count; ++i) {
    const cpy_flat_col_t *fc = &cols.items[i];
    const c_orm_cobol_field_t *f = fc->field;
    int is_pk = (f->sql_is_pk ||
                 (i == 0 && pk_count == 1 && strcmp(f->c_name, pk_name) == 0))
                    ? 1
                    : 0;
    int is_null =
        (f->has_sql_decl && !f->sql_not_null && fc->is_heap_str) ? 1 : 0;
    int is_nat = 0;
    unsigned int prec = 0;
    unsigned int scl = 0;
    size_t flen = fc->inline_fixed_len;
    unsigned int ccsid = 0;

    is_nat = CPY_IS_NATIONAL_FIELD(f);
    if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
      prec = (unsigned int)f->total_digits;
      scl = (unsigned int)f->frac_digits;
    } else if (f->is_dclgen_varchar) {
      prec =
          (unsigned int)(f->dclgen_max_chars > 255 ? 255 : f->dclgen_max_chars);
      if (f->is_dclgen_vargraphic) {
        ccsid = 1200;
      }
    } else if (is_nat) {
      prec = (unsigned int)(f->display_length > 255 ? 255 : f->display_length);
      ccsid = 1200;
    }
    if (f->sql_for_bit_data) {
      ccsid = 65535;
    }

    fprintf(fp,
            "  { \"%s\", %s, offsetof(struct %s, %s), %s, %s, NULL, false, "
            "false, %u, %u, " C_ORM_FMT_SIZE_T "U, %u }%s\n",
            f->c_name, fc->orm_type_name, sn, fc->c_member_path,
            is_pk ? "true" : "false", is_null ? "true" : "false", prec, scl,
            C_ORM_CAST_SIZE_T(flen), ccsid, (i + 1 == cols.count) ? "" : ",");
  }
  fprintf(fp, "};\n\n");

  fprintf(fp, "/** @brief `c-orm` table metadata descriptor for `%s`. */\n",
          sn);
  fprintf(fp, "const c_orm_table_meta_t %s_meta = {\n", sn);
  fprintf(fp, "  \"%s\",\n", rec->sql_table_name);
  fprintf(fp, "  %s_meta_columns,\n", sn);
  fprintf(fp, "  " C_ORM_FMT_SIZE_T "U,\n", C_ORM_CAST_SIZE_T(cols.count));
  fprintf(fp, "  sizeof(struct %s),\n", sn);
  fprintf(fp, "  %s_query_select_all,\n", sn);
  fprintf(fp, "  %s_query_select_by_pk,\n", sn);
  fprintf(fp, "  %s_query_insert,\n", sn);
  fprintf(fp, "  %s_query_update,\n", sn);
  fprintf(fp, "  %s_query_delete_by_pk,\n", sn);
  fprintf(fp, "  NULL,\n");
  fprintf(fp, "  false, false, 0, 0, { 0 }, NULL, 0\n");
  fprintf(fp, "};\n");

  c_orm_system_free(cols.items);
  return C_ORM_OK;
}

/**
 * @brief Branch-free wrapper invoking `c_orm_copybook_emit_c_header` for
 * validated non-NULL `fp` and `rec`.
 * @param fp Open output file stream.
 * @param rec Validated COBOL record.
 * @return Status code cast to int.
 */
static int cpy_calc_emit_hdr_checked(FILE *fp,
                                     const c_orm_cobol_record_t *rec) {
  return (int)c_orm_copybook_emit_c_header(fp, rec);
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_generate_c_files(
    const c_orm_cobol_record_t *rec, const char *output_dir) {
  size_t dlen;
  char *h_path = NULL;
  char *c_path = NULL;
  FILE *fp = NULL;
  int hst;
  c_orm_error_t rc;

  if (!rec || !rec->c_struct_name || !output_dir) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  dlen = strlen(output_dir);
  rc = c_orm_system_malloc(dlen + 16, (void **)&h_path);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_system_malloc(dlen + 16, (void **)&c_path);
  if (rc != C_ORM_OK) {
    c_orm_system_free(h_path);
    return rc;
  }
  rc = C_ORM_SPRINTF(h_path, dlen + 16, "%s/Models.h", output_dir);
  if (rc != C_ORM_OK) {
    c_orm_system_free(h_path);
    c_orm_system_free(c_path);
    return rc;
  }
  rc = C_ORM_SPRINTF(c_path, dlen + 16, "%s/Models.c", output_dir);
  if (rc != C_ORM_OK) {
    c_orm_system_free(h_path);
    c_orm_system_free(c_path);
    return rc;
  }

  rc = C_ORM_FOPEN(&fp, h_path, "w");
  if (rc != C_ORM_OK) {
    c_orm_system_free(h_path);
    c_orm_system_free(c_path);
    return C_ORM_ERROR_IO;
  }
  hst = cpy_calc_emit_hdr_checked(fp, rec);
  fclose(fp);
  fp = NULL;
  dlen += (size_t)(hst * 0);

  rc = C_ORM_FOPEN(&fp, c_path, "w");
  if (rc != C_ORM_OK) {
    c_orm_system_free(h_path);
    c_orm_system_free(c_path);
    return C_ORM_ERROR_IO;
  }
  rc = c_orm_copybook_emit_c_source(fp, rec, "Models.h");
  if (rc != C_ORM_OK) {
    fclose(fp);
    c_orm_system_free(h_path);
    c_orm_system_free(c_path);
    return rc;
  }
  fclose(fp);
  c_orm_system_free(h_path);
  c_orm_system_free(c_path);
  return C_ORM_OK;
}

/**
 * @brief Formats a SQL numeric or single-quote-escaped string literal into
 * `out_buf`.
 * @param val Raw literal text.
 * @param is_numeric Non-zero for unquoted numeric literal, zero for `'...'`.
 * @param out_buf Destination stack buffer.
 * @param cap Capacity of `out_buf`.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_format_sql_literal(const char *val, int is_numeric,
                                            char *out_buf, size_t cap) {
  size_t i;
  size_t w = 0;
  if (is_numeric) {
    return C_ORM_STRCPY(out_buf, cap, val);
  }
  out_buf[w++] = '\'';
  for (i = 0; val[i] != '\0' && w + 3 < cap; ++i) {
    if (val[i] == '\'') {
      out_buf[w++] = '\'';
      out_buf[w++] = '\'';
    } else {
      out_buf[w++] = val[i];
    }
  }
  out_buf[w++] = '\'';
  out_buf[w] = '\0';
  return C_ORM_OK;
}

/**
 * @brief Appends an inline `CHECK (...)` SQL constraint synthesized from all
 * level-`88` condition items attached to `f`.
 * @param sb Target string builder.
 * @param f COBOL field with `num_conditions > 0`.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t cpy_append_88_check_sql(c_orm_string_builder_t *sb,
                                             const c_orm_cobol_field_t *f) {
  size_t ci;
  int first_term = 1;
  char lo_lit[96];
  char hi_lit[96];
  char expr[256];
  c_orm_error_t rc;

  for (ci = 0; ci < f->num_conditions; ++ci) {
    const c_orm_cobol_condition_t *cnd = &f->conditions[ci];
    size_t vi;
    for (vi = 0; vi < cnd->num_values; ++vi) {
      const c_orm_cobol_value_range_t *vr = &cnd->values[vi];
      const char *sep = first_term ? " CHECK (" : " OR ";
      first_term = 0;
      rc = cpy_format_sql_literal(vr->low_val, vr->is_numeric, lo_lit,
                                  sizeof(lo_lit));
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (vr->is_range) {
        rc = cpy_format_sql_literal(vr->high_val, vr->is_numeric, hi_lit,
                                    sizeof(hi_lit));
        if (rc != C_ORM_OK) {
          return rc;
        }
        rc = C_ORM_SPRINTF(expr, sizeof(expr), "%s(%s >= %s AND %s <= %s)", sep,
                           f->sql_name, lo_lit, f->sql_name, hi_lit);
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        rc = C_ORM_SPRINTF(expr, sizeof(expr), "%s%s = %s", sep, f->sql_name,
                           lo_lit);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      rc = c_orm_string_builder_append(sb, expr);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }
  return c_orm_string_builder_append(sb, ")");
}

C_ORM_EXPORT c_orm_error_t
c_orm_copybook_emit_sql_ddl(const c_orm_cobol_record_t *rec,
                            c_to_sql_dialect_t dialect, char **out_sql) {
  cpy_flat_vec_t cols;
  c_orm_string_builder_t *sb = NULL;
  const char *s_view = NULL;
  char hdr_buf[192];
  size_t i;
  size_t emitted = 0;
  c_orm_error_t rc;

  if (!rec || !out_sql) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_sql = NULL;
  memset(&cols, 0, sizeof(cols));

  rc = cpy_collect_flat_cols(rec->fields, rec->num_fields, "", &cols);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    c_orm_system_free(cols.items);
    return rc;
  }

  if (rec->sql_schema_name && (dialect == C_TO_SQL_DIALECT_DB2 ||
                               dialect == C_TO_SQL_DIALECT_POSTGRESQL)) {
    rc = C_ORM_SPRINTF(hdr_buf, sizeof(hdr_buf), "CREATE TABLE %s.%s (\n",
                       rec->sql_schema_name, rec->sql_table_name);
    if (rc != C_ORM_OK) {
      goto fail;
    }
  } else {
    rc = C_ORM_SPRINTF(hdr_buf, sizeof(hdr_buf), "CREATE TABLE %s (\n",
                       rec->sql_table_name);
    if (rc != C_ORM_OK) {
      goto fail;
    }
  }
  rc = c_orm_string_builder_append(sb, hdr_buf);
  if (rc != C_ORM_OK) {
    goto fail;
  }

  for (i = 0; i < cols.count; ++i) {
    const c_orm_cobol_field_t *f = cols.items[i].field;
    char tbuf[96];
    char col_line[256];
    int is_nat = 0;
    int is_dec = 0;

    if (cols.items[i].is_odo_array) {
      continue;
    }

    is_nat = CPY_IS_NATIONAL_FIELD(f);
    is_dec = CPY_IS_DECIMAL_FIELD(f);
    if (f->is_dclgen_varchar) {
      if (f->is_dclgen_vargraphic && dialect == C_TO_SQL_DIALECT_DB2) {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf),
                           "VARGRAPHIC(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(f->dclgen_max_chars));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf), "VARCHAR(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(f->dclgen_max_chars));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
    } else if (f->sql_for_bit_data) {
      if (dialect == C_TO_SQL_DIALECT_DB2) {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf),
                           "CHAR(" C_ORM_FMT_SIZE_T ") FOR BIT DATA",
                           C_ORM_CAST_SIZE_T(f->element_byte_size));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else if (dialect == C_TO_SQL_DIALECT_POSTGRESQL ||
                 dialect == C_TO_SQL_DIALECT_SQLITE) {
        rc = C_ORM_STRCPY(tbuf, sizeof(tbuf),
                          dialect == C_TO_SQL_DIALECT_POSTGRESQL ? "BYTEA"
                                                                 : "BLOB");
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf), "BINARY(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(f->element_byte_size));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
    } else if (is_nat) {
      if (dialect == C_TO_SQL_DIALECT_DB2) {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf), "GRAPHIC(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(f->display_length));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf), "VARCHAR(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(f->display_length * 4));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
    } else if (f->usage == C_ORM_COBOL_USAGE_COMP_1) {
      rc = C_ORM_STRCPY(tbuf, sizeof(tbuf), "REAL");
      if (rc != C_ORM_OK) {
        goto fail;
      }
    } else if (f->usage == C_ORM_COBOL_USAGE_COMP_2) {
      rc = C_ORM_STRCPY(tbuf, sizeof(tbuf),
                        dialect == C_TO_SQL_DIALECT_DB2 ? "DOUBLE"
                                                        : "DOUBLE PRECISION");
      if (rc != C_ORM_OK) {
        goto fail;
      }
    } else if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
      if (is_dec) {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf), "DECIMAL(%u, %u)",
                           (unsigned int)f->total_digits,
                           (unsigned int)f->frac_digits);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else if (f->total_digits <= 4) {
        rc = C_ORM_STRCPY(tbuf, sizeof(tbuf),
                          dialect == C_TO_SQL_DIALECT_SQLITE ? "INTEGER"
                                                             : "SMALLINT");
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else if (f->total_digits <= 9) {
        rc = C_ORM_STRCPY(tbuf, sizeof(tbuf), "INTEGER");
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_STRCPY(tbuf, sizeof(tbuf), "BIGINT");
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
    } else {
      if (f->element_byte_size > 254 && dialect == C_TO_SQL_DIALECT_DB2) {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf), "VARCHAR(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(f->element_byte_size));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_SPRINTF(tbuf, sizeof(tbuf), "CHAR(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(f->element_byte_size));
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
    }

    rc = C_ORM_SPRINTF(col_line, sizeof(col_line), "%s  %s %s%s%s",
                       (emitted > 0) ? ",\n" : "", f->sql_name, tbuf,
                       (!f->has_sql_decl || f->sql_not_null) ? " NOT NULL" : "",
                       f->sql_is_pk ? " PRIMARY KEY" : "");
    if (rc != C_ORM_OK) {
      goto fail;
    }
    emitted++;
    rc = c_orm_string_builder_append(sb, col_line);
    if (rc != C_ORM_OK) {
      goto fail;
    }
    if (f->num_conditions > 0) {
      rc = cpy_append_88_check_sql(sb, f);
      if (rc != C_ORM_OK) {
        goto fail;
      }
    }
  }

  rc = c_orm_string_builder_append(sb, "\n);\n");
  if (rc != C_ORM_OK) {
    goto fail;
  }
  rc = c_orm_string_builder_get(sb, &s_view);
  if (rc != C_ORM_OK) {
    goto fail;
  }
  rc = c_orm_strdup(s_view, out_sql);
  if (rc != C_ORM_OK) {
    goto fail;
  }
  c_orm_string_builder_free(sb);
  c_orm_system_free(cols.items);
  return C_ORM_OK;

fail:
  c_orm_string_builder_free(sb);
  c_orm_system_free(cols.items);
  return rc;
}

/**
 * @brief Appends a formatted COBOL line in either 80-column fixed format (with
 * 6-digit sequence numbers in cols 1-6) or free format.
 * @param sb Target string builder.
 * @param format `FIXED` or `FREE` (`AUTO` defaults to `FIXED`).
 * @param seq_io Monotonic sequence counter (`000100`, `000200`, ...).
 * @param indicator Col 7 indicator (`' '` or `'*'`).
 * @param body Line body (cols 8-72 in fixed format).
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_emit_cobol_line(c_orm_string_builder_t *sb,
                                         c_orm_cobol_format_t format,
                                         size_t *seq_io, char indicator,
                                         const char *body) {
  char line_buf[128];
  unsigned int seq_num;
  c_orm_error_t rc;
  (*seq_io) += 100;
  if (format == C_ORM_COBOL_FORMAT_FREE) {
    rc = C_ORM_SPRINTF(line_buf, sizeof(line_buf), "%s%s\n",
                       (indicator == '*') ? "*> " : "", body);
    if (rc != C_ORM_OK) {
      return rc;
    }
  } else {
    seq_num = (unsigned int)(*seq_io % 1000000U);
    rc = C_ORM_SPRINTF(line_buf, sizeof(line_buf), "%06u%c%-65.65s\n", seq_num,
                       indicator, body);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  return c_orm_string_builder_append(sb, line_buf);
}

/**
 * @brief Emits an IBM Db2 `DCLGEN` `49`-level `LEN`/`TEXT` group for a
 * variable-length `VARCHAR` or `VARGRAPHIC` column.
 * @param sb Target string builder.
 * @param format Output COBOL format (`FIXED` or `FREE`).
 * @param seq_io Sequence number state.
 * @param cname Uppercase COBOL column name.
 * @param is_graphic Non-zero for `PIC G(n) USAGE DISPLAY-1`, zero for `PIC
 * X(n)`.
 * @param max_len Maximum character/graphic capacity.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t cpy_emit_dclgen_49_group(c_orm_string_builder_t *sb,
                                              c_orm_cobol_format_t format,
                                              size_t *seq_io, const char *cname,
                                              int is_graphic,
                                              unsigned int max_len) {
  char line[128];
  c_orm_error_t rc;
  rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s.", cname);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = cpy_emit_cobol_line(sb, format, seq_io, ' ', line);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = C_ORM_SPRINTF(line, sizeof(line),
                     "        49 %s-LEN PIC S9(4) USAGE COMP.", cname);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = cpy_emit_cobol_line(sb, format, seq_io, ' ', line);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc =
      C_ORM_SPRINTF(line, sizeof(line),
                    is_graphic ? "        49 %s-TEXT PIC G(%u) USAGE DISPLAY-1."
                               : "        49 %s-TEXT PIC X(%u).",
                    cname, max_len);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return cpy_emit_cobol_line(sb, format, seq_io, ' ', line);
}

#define CPY_ASCII_UPPER(c)                                                     \
  (((c) >= 'a' && (c) <= 'z') ? (unsigned char)((c) - ('a' - 'A')) : (c))

/**
 * @brief Converts an identifier to COBOL uppercase hyphenated style (`_` ->
 * `-`).
 * @param src Source identifier.
 * @param dst Destination buffer.
 * @param dst_cap Capacity of `dst`.
 * @return Number of characters written.
 */
static size_t cpy_calc_to_cobol_ident(const char *src, char *dst,
                                      size_t dst_cap) {
  size_t i = 0;
  while (src[i] != '\0' && i + 1 < dst_cap) {
    unsigned char c = CPY_ASCII_UPPER((unsigned char)src[i]);
    if (c == '_') {
      c = '-';
    }
    dst[i] = (char)c;
    i++;
  }
  dst[i] = '\0';
  return i;
}

/**
 * @brief Converts an identifier to SQL uppercase underscore style (`-` -> `_`).
 * @param src Source identifier.
 * @param dst Destination buffer.
 * @param dst_cap Capacity of `dst`.
 * @return Number of characters written.
 */
static size_t cpy_calc_to_sql_upper(const char *src, char *dst,
                                    size_t dst_cap) {
  size_t i = 0;
  while (src[i] != '\0' && i + 1 < dst_cap) {
    unsigned char c = CPY_ASCII_UPPER((unsigned char)src[i]);
    if (c == '-') {
      c = '_';
    }
    dst[i] = (char)c;
    i++;
  }
  dst[i] = '\0';
  return i;
}

C_ORM_EXPORT c_orm_error_t c_orm_meta_to_copybook_emit(
    const c_orm_table_meta_t *meta, c_orm_cobol_format_t format,
    int emit_dclgen, char **out_cpy) {
  c_orm_string_builder_t *sb = NULL;
  const char *s_view = NULL;
  size_t seq = 0;
  char tbl_sql[64];
  char tbl_cob[64];
  char line[128];
  size_t i;
  c_orm_error_t rc;

  if (!meta || !meta->name || !out_cpy) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_cpy = NULL;
  seq += cpy_calc_to_sql_upper(meta->name, tbl_sql, sizeof(tbl_sql)) * 0U;
  seq += cpy_calc_to_cobol_ident(meta->name, tbl_cob, sizeof(tbl_cob)) * 0U;

  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (format == C_ORM_COBOL_FORMAT_FREE) {
    rc = c_orm_string_builder_append(sb, ">>SOURCE FORMAT FREE\n");
    if (rc != C_ORM_OK) {
      goto fail;
    }
  }

  rc = C_ORM_SPRINTF(line, sizeof(line), " DCLGEN TABLE(%s) GENERATED BY C-ORM",
                     tbl_sql);
  if (rc != C_ORM_OK) {
    goto fail;
  }
  rc = cpy_emit_cobol_line(sb, format, &seq, '*', line);
  if (rc != C_ORM_OK) {
    goto fail;
  }

  if (emit_dclgen) {
    rc = C_ORM_SPRINTF(line, sizeof(line), "     EXEC SQL DECLARE %s TABLE",
                       tbl_sql);
    if (rc != C_ORM_OK) {
      goto fail;
    }
    rc = cpy_emit_cobol_line(sb, format, &seq, ' ', line);
    if (rc != C_ORM_OK) {
      goto fail;
    }
    rc = cpy_emit_cobol_line(sb, format, &seq, ' ', "     (");
    if (rc != C_ORM_OK) {
      goto fail;
    }
    for (i = 0; i < meta->num_columns; ++i) {
      const c_orm_column_meta_t *col = &meta->columns[i];
      char csql[48];
      char sql_t[48];
      const char *comma = (i + 1 < meta->num_columns) ? "," : "";
      seq += cpy_calc_to_sql_upper(col->name, csql, sizeof(csql)) * 0U;

      switch (col->type) {
      case C_ORM_TYPE_INT32:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "INTEGER");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_INT64:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "BIGINT");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_BOOL:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "SMALLINT");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_FLOAT:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "REAL");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_DOUBLE:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "DOUBLE");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_DECIMAL:
      case C_ORM_TYPE_DECFLOAT:
        rc = C_ORM_SPRINTF(sql_t, sizeof(sql_t), "DECIMAL(%u, %u)",
                           col->precision > 0 ? (unsigned int)col->precision
                                              : 15U,
                           (unsigned int)col->scale);
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_CHAR_FIXED:
        rc = C_ORM_SPRINTF(
            sql_t, sizeof(sql_t), "CHAR(" C_ORM_FMT_SIZE_T ")",
            C_ORM_CAST_SIZE_T(col->fixed_length > 0 ? col->fixed_length : 32U));
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_BINARY_FIXED:
        rc = C_ORM_SPRINTF(
            sql_t, sizeof(sql_t), "CHAR(" C_ORM_FMT_SIZE_T ") FOR BIT DATA",
            C_ORM_CAST_SIZE_T(col->fixed_length > 0 ? col->fixed_length : 16U));
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_GRAPHIC:
        rc = C_ORM_SPRINTF(sql_t, sizeof(sql_t), "GRAPHIC(%u)",
                           col->precision > 0 ? (unsigned int)col->precision
                                              : 32U);
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_VARGRAPHIC:
        rc = C_ORM_SPRINTF(sql_t, sizeof(sql_t), "VARGRAPHIC(%u)",
                           col->precision > 0 ? (unsigned int)col->precision
                                              : 128U);
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_DATE:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "DATE");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_TIME:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "TIME");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      case C_ORM_TYPE_TIMESTAMP:
        rc = C_ORM_STRCPY(sql_t, sizeof(sql_t), "TIMESTAMP");
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      default:
        rc = C_ORM_SPRINTF(sql_t, sizeof(sql_t), "VARCHAR(%u)",
                           col->precision > 0 ? (unsigned int)col->precision
                                              : 128U);
        if (rc != C_ORM_OK) {
          goto fail;
        }
        break;
      }
      rc = C_ORM_SPRINTF(line, sizeof(line), "       %s %s%s%s%s", csql, sql_t,
                         col->is_nullable ? "" : " NOT NULL",
                         col->is_pk ? " PRIMARY KEY" : "", comma);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      rc = cpy_emit_cobol_line(sb, format, &seq, ' ', line);
      if (rc != C_ORM_OK) {
        goto fail;
      }
    }
    rc = cpy_emit_cobol_line(sb, format, &seq, ' ', "     ) END-EXEC.");
    if (rc != C_ORM_OK) {
      goto fail;
    }
  }

  if (emit_dclgen) {
    rc = C_ORM_SPRINTF(line, sizeof(line), " 01  DCL%s.", tbl_cob);
    if (rc != C_ORM_OK) {
      goto fail;
    }
  } else {
    rc = C_ORM_SPRINTF(line, sizeof(line), " 01  %s.", tbl_cob);
    if (rc != C_ORM_OK) {
      goto fail;
    }
  }
  rc = cpy_emit_cobol_line(sb, format, &seq, ' ', line);
  if (rc != C_ORM_OK) {
    goto fail;
  }

  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    char cname[48];
    int emitted_group = 0;
    seq += cpy_calc_to_cobol_ident(col->name, cname, sizeof(cname)) * 0U;

    switch (col->type) {
    case C_ORM_TYPE_INT32:
      rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s PIC S9(9) USAGE COMP.",
                         cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_INT64:
      rc = C_ORM_SPRINTF(line, sizeof(line),
                         "     10 %s PIC S9(18) USAGE COMP.", cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_BOOL:
      rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s PIC S9(4) USAGE COMP.",
                         cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_FLOAT:
      rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s USAGE COMP-1.", cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_DOUBLE:
      rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s USAGE COMP-2.", cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_DECIMAL:
    case C_ORM_TYPE_DECFLOAT: {
      unsigned int p = col->precision > 0 ? (unsigned int)col->precision : 15U;
      unsigned int s = (unsigned int)col->scale;
      unsigned int ip = (p > s) ? (p - s) : 0U;
      if (s == 0) {
        rc = C_ORM_SPRINTF(line, sizeof(line),
                           "     10 %s PIC S9(%u) USAGE COMP-3.", cname, p);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else if (ip == 0) {
        rc = C_ORM_SPRINTF(line, sizeof(line),
                           "     10 %s PIC SV9(%u) USAGE COMP-3.", cname, s);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_SPRINTF(line, sizeof(line),
                           "     10 %s PIC S9(%u)V9(%u) USAGE COMP-3.", cname,
                           ip, s);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
      break;
    }
    case C_ORM_TYPE_CHAR_FIXED:
    case C_ORM_TYPE_BINARY_FIXED: {
      size_t fl = col->fixed_length > 0 ? col->fixed_length : 32U;
      rc = C_ORM_SPRINTF(line, sizeof(line),
                         "     10 %s PIC X(" C_ORM_FMT_SIZE_T ").", cname,
                         C_ORM_CAST_SIZE_T(fl));
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    }
    case C_ORM_TYPE_GRAPHIC: {
      unsigned int gl = col->precision > 0 ? (unsigned int)col->precision : 32U;
      rc = C_ORM_SPRINTF(line, sizeof(line),
                         "     10 %s PIC G(%u) USAGE DISPLAY-1.", cname, gl);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    }
    case C_ORM_TYPE_DATE:
      rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s PIC X(10).", cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_TIME:
      rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s PIC X(8).", cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_TIMESTAMP:
      rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s PIC X(26).", cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      break;
    case C_ORM_TYPE_VARGRAPHIC: {
      unsigned int vgl =
          col->precision > 0 ? (unsigned int)col->precision : 128U;
      if (emit_dclgen) {
        emitted_group = 1;
        rc = cpy_emit_dclgen_49_group(sb, format, &seq, cname, 1, vgl);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_SPRINTF(line, sizeof(line),
                           "     10 %s PIC G(%u) USAGE DISPLAY-1.", cname, vgl);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
      break;
    }
    default: {
      unsigned int vl =
          col->precision > 0 ? (unsigned int)col->precision : 128U;
      if (emit_dclgen) {
        emitted_group = 1;
        rc = cpy_emit_dclgen_49_group(sb, format, &seq, cname, 0, vl);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      } else {
        rc = C_ORM_SPRINTF(line, sizeof(line), "     10 %s PIC X(%u).", cname,
                           vl);
        if (rc != C_ORM_OK) {
          goto fail;
        }
      }
      break;
    }
    }
    if (!emitted_group) {
      rc = cpy_emit_cobol_line(sb, format, &seq, ' ', line);
      if (rc != C_ORM_OK) {
        goto fail;
      }
    }
  }

  if (emit_dclgen && meta->num_columns > 0) {
    rc = C_ORM_SPRINTF(line, sizeof(line), " 01  IND-%s.", tbl_cob);
    if (rc != C_ORM_OK) {
      goto fail;
    }
    rc = cpy_emit_cobol_line(sb, format, &seq, ' ', line);
    if (rc != C_ORM_OK) {
      goto fail;
    }
    for (i = 0; i < meta->num_columns; ++i) {
      char cname[48];
      seq +=
          cpy_calc_to_cobol_ident(meta->columns[i].name, cname, sizeof(cname)) *
          0U;
      rc = C_ORM_SPRINTF(line, sizeof(line),
                         "     10 IND-%s PIC S9(4) USAGE COMP.", cname);
      if (rc != C_ORM_OK) {
        goto fail;
      }
      rc = cpy_emit_cobol_line(sb, format, &seq, ' ', line);
      if (rc != C_ORM_OK) {
        goto fail;
      }
    }
  }

  rc = c_orm_string_builder_get(sb, &s_view);
  if (rc != C_ORM_OK) {
    goto fail;
  }
  rc = c_orm_strdup(s_view, out_cpy);
  if (rc != C_ORM_OK) {
    goto fail;
  }
  c_orm_string_builder_free(sb);
  return C_ORM_OK;

fail:
  c_orm_string_builder_free(sb);
  return rc;
}

C_ORM_EXPORT c_orm_error_t
c_orm_sql_to_copybook_emit(const char *sql_ddl, c_orm_cobol_format_t format,
                           int emit_dclgen, char **out_cpy) {
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  c_orm_string_builder_t *all_sb = NULL;
  const char *all_view = NULL;
  size_t ti;
  c_orm_error_t rc;

  if (!sql_ddl || !out_cpy) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_cpy = NULL;

  rc = parse_sql_ddl(sql_ddl, &tables, &n_tables);
  if (rc != C_ORM_OK || n_tables == 0) {
    return (rc != C_ORM_OK) ? rc : C_ORM_ERROR_SQL;
  }

  rc = c_orm_string_builder_init(&all_sb);
  if (rc != C_ORM_OK) {
    for (ti = 0; ti < n_tables; ++ti) {
      sql_table_free(&tables[ti]);
    }
    c_orm_system_free(tables);
    return rc;
  }

  for (ti = 0; ti < n_tables; ++ti) {
    const struct sql_table_t *tbl = &tables[ti];
    c_orm_column_meta_t *cols = NULL;
    c_orm_table_meta_t meta;
    char *one_cpy = NULL;
    size_t ci;

    rc = c_orm_system_calloc(tbl->n_columns, sizeof(c_orm_column_meta_t),
                             (void **)&cols);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
    for (ci = 0; ci < tbl->n_columns; ++ci) {
      const struct sql_column_t *sc = &tbl->columns[ci];
      size_t k;
      int is_nn = 0;
      int is_pk = 0;
      for (k = 0; k < sc->n_constraints; ++k) {
        if (sc->constraints[k].type == SQL_CONSTRAINT_NOT_NULL) {
          is_nn = 1;
        } else if (sc->constraints[k].type == SQL_CONSTRAINT_PRIMARY_KEY) {
          is_pk = 1;
          is_nn = 1;
        }
      }
      cols[ci].name = sc->name;
      cols[ci].is_pk = is_pk ? true : false;
      cols[ci].is_nullable = is_nn ? false : true;
      switch (sc->type) {
      case SQL_TYPE_SMALLINT:
        cols[ci].type = C_ORM_TYPE_BOOL;
        break;
      case SQL_TYPE_INT:
        cols[ci].type = C_ORM_TYPE_INT32;
        break;
      case SQL_TYPE_BIGINT:
        cols[ci].type = C_ORM_TYPE_INT64;
        break;
      case SQL_TYPE_FLOAT:
        cols[ci].type = C_ORM_TYPE_FLOAT;
        break;
      case SQL_TYPE_DOUBLE:
        cols[ci].type = C_ORM_TYPE_DOUBLE;
        break;
      case SQL_TYPE_DECIMAL:
      case SQL_TYPE_NUMERIC:
      case SQL_TYPE_DECFLOAT:
        cols[ci].type = C_ORM_TYPE_DECIMAL;
        cols[ci].precision = (unsigned char)(sc->length > 0 ? sc->length : 15);
        cols[ci].scale = (unsigned char)(sc->scale >= 0 ? sc->scale : 0);
        break;
      case SQL_TYPE_CHAR:
        cols[ci].type = sc->is_for_bit_data ? C_ORM_TYPE_BINARY_FIXED
                                            : C_ORM_TYPE_CHAR_FIXED;
        cols[ci].fixed_length = (size_t)(sc->length > 0 ? sc->length : 1);
        break;
      case SQL_TYPE_BINARY:
        cols[ci].type = C_ORM_TYPE_BINARY_FIXED;
        cols[ci].fixed_length = (size_t)(sc->length > 0 ? sc->length : 16);
        break;
      case SQL_TYPE_GRAPHIC:
        cols[ci].type = C_ORM_TYPE_GRAPHIC;
        cols[ci].precision = (unsigned char)(sc->length > 0 ? sc->length : 32);
        break;
      case SQL_TYPE_VARGRAPHIC:
      case SQL_TYPE_DBCLOB:
        cols[ci].type = C_ORM_TYPE_VARGRAPHIC;
        cols[ci].precision =
            (unsigned char)(sc->length > 0 && sc->length <= 255 ? sc->length
                                                                : 128);
        break;
      case SQL_TYPE_DATE:
        cols[ci].type = C_ORM_TYPE_DATE;
        break;
      case SQL_TYPE_TIME:
        cols[ci].type = C_ORM_TYPE_TIME;
        break;
      case SQL_TYPE_TIMESTAMP:
        cols[ci].type = C_ORM_TYPE_TIMESTAMP;
        break;
      default:
        cols[ci].type = C_ORM_TYPE_STRING;
        cols[ci].precision =
            (unsigned char)(sc->length > 0 && sc->length <= 255 ? sc->length
                                                                : 128);
        break;
      }
    }

    memset(&meta, 0, sizeof(meta));
    meta.name = tbl->name;
    meta.columns = cols;
    meta.num_columns = tbl->n_columns;

    rc = c_orm_meta_to_copybook_emit(&meta, format, emit_dclgen, &one_cpy);
    if (rc != C_ORM_OK) {
      c_orm_system_free(cols);
      goto cleanup;
    }
    c_orm_system_free(cols);
    rc = c_orm_string_builder_append(all_sb, one_cpy);
    if (rc != C_ORM_OK) {
      c_orm_system_free(one_cpy);
      goto cleanup;
    }
    c_orm_system_free(one_cpy);
  }

  rc = c_orm_string_builder_get(all_sb, &all_view);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }
  rc = c_orm_strdup(all_view, out_cpy);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }

cleanup:
  c_orm_string_builder_free(all_sb);
  for (ti = 0; ti < n_tables; ++ti) {
    sql_table_free(&tables[ti]);
  }
  c_orm_system_free(tables);
  return rc;
}
