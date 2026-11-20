/**
 * @file c_orm_no_discard.h
 * @brief Macro definitions for nodiscard / warn_unused_result attributes.
 * @defgroup c_orm_no_discard No Discard Macros
 * @{
 */

#ifndef C_ORM_NO_DISCARD_H
#define C_ORM_NO_DISCARD_H

/* clang-format off */
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef NO_DISCARD
#if defined(__cplusplus) && __cplusplus >= 201703L
#define NO_DISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#define NO_DISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && _MSC_VER >= 1700
#define NO_DISCARD _Check_return_
#else
#define NO_DISCARD
#endif
#endif

#ifndef NO_DISCARD_TYPE
#if defined(__cplusplus) && __cplusplus >= 201703L
#define NO_DISCARD_TYPE [[nodiscard]]
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define NO_DISCARD_TYPE [[nodiscard]]
#else
#define NO_DISCARD_TYPE
#endif
#endif

#ifndef NO_DISCARD_TYPEDEF
#if !defined(__cplusplus) && defined(__clang__)
#define NO_DISCARD_TYPEDEF __attribute__((warn_unused_result))
#else
#define NO_DISCARD_TYPEDEF
#endif
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_NO_DISCARD_H */
