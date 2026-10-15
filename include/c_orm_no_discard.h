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

#if defined(__GNUC__) || defined(__clang__)
#define NO_DISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER) && _MSC_VER >= 1700
#define NO_DISCARD _Check_return_
#else
#define NO_DISCARD
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_NO_DISCARD_H */
