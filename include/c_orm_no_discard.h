#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_no_discard.h
 * @brief Macro definitions for nodiscard / warn_unused_result attributes.
 * @defgroup c_orm_no_discard No Discard Macros
 * @{
 */

#ifndef C_ORM_NO_DISCARD_H
#define C_ORM_NO_DISCARD_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus) && __cplusplus >= 201703L
#define NO_DISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#define NO_DISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && _MSC_VER >= 1700
#define NO_DISCARD _Check_return_
#else
#define NO_DISCARD
#endif

#ifdef __cplusplus
}
#endif

#endif /* C_ORM_NO_DISCARD_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
