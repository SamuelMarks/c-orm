#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file parson.h
 * @brief Lightweight C JSON library.
 * @defgroup parson Parson JSON Library
 * @{
 */
/*
 SPDX-License-Identifier: MIT

 Parson 1.5.3 (https://github.com/kgabis/parson)
 Copyright (c) 2012 - 2023 Krzysztof Gabis

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
*/

#ifndef parson_parson_h
#define parson_parson_h

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#if 0
} /* unconfuse xcode */
#endif

#define PARSON_VERSION_MAJOR 1
#define PARSON_VERSION_MINOR 5
#define PARSON_VERSION_PATCH 3

#define PARSON_VERSION_STRING "1.5.3"

/* clang-format off */
#include <stddef.h> /* size_t */
/* clang-format on */

/* Types and enums */
typedef struct json_object_t JSON_Object;
typedef struct json_array_t JSON_Array;
typedef struct json_value_t JSON_Value;

enum json_value_type {
  JSONError = -1,
  JSONNull = 1,
  JSONString = 2,
  JSONNumber = 3,
  JSONObject = 4,
  JSONArray = 5,
  JSONBoolean = 6
};
typedef int JSON_Value_Type;

enum json_result_t { JSONSuccess = 0, JSONFailure = -1 };
typedef int JSON_Status;

typedef void *(*JSON_Malloc_Function)(size_t);
typedef void (*JSON_Free_Function)(void *);

/* A function used for serializing numbers (see
   json_set_number_serialization_function). If 'buf' is null then it should
   return number of bytes that would've been written (but not more than
   PARSON_NUM_BUF_SIZE).
*/
typedef int (*JSON_Number_Serialization_Function)(double num, char *buf);

/* Call only once, before calling any other function from parson API. If not
   called, malloc and free from stdlib will be used for all allocations */
/**
 * @brief Set custom memory allocation functions.
 *
 * @param malloc_fun Custom malloc function.
 * @param free_fun Custom free function.
 */
void json_set_allocation_functions(JSON_Malloc_Function malloc_fun,
                                   JSON_Free_Function free_fun);

/* Sets if slashes should be escaped or not when serializing JSON. By default
 slashes are escaped. This function sets a global setting and is not thread
 safe. */
/**
 * @brief Set whether slashes should be escaped when serializing.
 *
 * @param escape_slashes Non-zero to escape slashes, 0 otherwise.
 */
void json_set_escape_slashes(int escape_slashes);

/* Sets float format used for serialization of numbers.
   Make sure it can't serialize to a string longer than PARSON_NUM_BUF_SIZE.
   If format is null then the default format is used. */
/**
 * @brief Set float format used for serialization.
 *
 * @param format Printf-compatible float format specifier.
 */
void json_set_float_serialization_format(const char *format);

/* Sets a function that will be used for serialization of numbers.
   If function is null then the default serialization function is used. */
/**
 * @brief Set custom number serialization function.
 *
 * @param fun Custom number serialization function.
 */
void json_set_number_serialization_function(
    JSON_Number_Serialization_Function fun);

/* Parses first JSON value in a file, returns NULL in case of error */
/**
 * @brief Parse JSON value from a file.
 *
 * @param filename Path to file to parse.
 * @return Parsed JSON_Value on success or NULL on failure.
 */
JSON_Value *json_parse_file(const char *filename);

/* Parses first JSON value in a file and ignores comments (/ * * / and //),
   returns NULL in case of error */
/**
 * @brief Parse JSON value from a file ignoring comments.
 *
 * @param filename Path to file to parse.
 * @return Parsed JSON_Value on success or NULL on failure.
 */
JSON_Value *json_parse_file_with_comments(const char *filename);

/*  Parses first JSON value in a string, returns NULL in case of error */
/**
 * @brief Parse JSON value from a string.
 *
 * @param string Input string containing JSON.
 * @return Parsed JSON_Value on success or NULL on failure.
 */
JSON_Value *json_parse_string(const char *string);

/*  Parses first JSON value in a string and ignores comments (/ * * / and //),
    returns NULL in case of error */
/**
 * @brief Parse JSON value from a string ignoring comments.
 *
 * @param string Input string containing JSON.
 * @return Parsed JSON_Value on success or NULL on failure.
 */
JSON_Value *json_parse_string_with_comments(const char *string);

/* Serialization */
/**
 * @brief Compute required buffer size for JSON serialization.
 *
 * @param value JSON value to serialize.
 * @return Size in bytes, or 0 on failure.
 */
size_t json_serialization_size(const JSON_Value *value); /* returns 0 on fail */
/**
 * @brief Serialize JSON value to provided buffer.
 *
 * @param value JSON value to serialize.
 * @param buf Output buffer.
 * @param buf_size_in_bytes Size of buffer in bytes.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_serialize_to_buffer(const JSON_Value *value, char *buf,
                                     size_t buf_size_in_bytes);
/**
 * @brief Serialize JSON value to file.
 *
 * @param value JSON value to serialize.
 * @param filename Output file path.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_serialize_to_file(const JSON_Value *value,
                                   const char *filename);
/**
 * @brief Serialize JSON value to newly allocated string.
 *
 * @param value JSON value to serialize.
 * @return Serialized string or NULL on failure.
 */
char *json_serialize_to_string(const JSON_Value *value);

/* Pretty serialization */
size_t
/**
 * @brief Compute required buffer size for pretty JSON serialization.
 *
 * @param value JSON value to serialize.
 * @return Size in bytes, or 0 on failure.
 */
json_serialization_size_pretty(const JSON_Value *value); /* returns 0 on fail */
/**
 * @brief Serialize JSON value with formatting to buffer.
 *
 * @param value JSON value to serialize.
 * @param buf Output buffer.
 * @param buf_size_in_bytes Size of buffer in bytes.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_serialize_to_buffer_pretty(const JSON_Value *value, char *buf,
                                            size_t buf_size_in_bytes);
/**
 * @brief Serialize JSON value with formatting to file.
 *
 * @param value JSON value to serialize.
 * @param filename Output file path.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_serialize_to_file_pretty(const JSON_Value *value,
                                          const char *filename);
/**
 * @brief Serialize JSON value with formatting to string.
 *
 * @param value JSON value to serialize.
 * @return Formatted string or NULL on failure.
 */
char *json_serialize_to_string_pretty(const JSON_Value *value);

/**
 * @brief Free string allocated by json_serialize_to_string.
 *
 * @param string String to free.
 */
void json_free_serialized_string(
    char *string); /* frees string from json_serialize_to_string and
                      json_serialize_to_string_pretty */

/* Comparing */
/**
 * @brief Check if two JSON values are deeply equal.
 *
 * @param a First JSON value.
 * @param b Second JSON value.
 * @return 1 if equal, 0 if not equal.
 */
int json_value_equals(const JSON_Value *a, const JSON_Value *b);

/* Validation
   This is *NOT* JSON Schema. It validates json by checking if object have
   identically named fields with matching types. For example schema {"name":"",
   "age":0} will validate
   {"name":"Joe", "age":25} and {"name":"Joe", "age":25, "gender":"m"},
   but not {"name":"Joe"} or {"name":"Joe", "age":"Cucumber"}.
   In case of arrays, only first value in schema is checked against all values
   in tested array. Empty objects ({}) validate all objects, empty arrays ([])
   validate all arrays, null validates values of every type.
 */
/**
 * @brief Validate JSON value against schema.
 *
 * @param schema JSON schema.
 * @param value JSON value to validate.
 * @return JSONSuccess if valid, JSONFailure otherwise.
 */
JSON_Status json_validate(const JSON_Value *schema, const JSON_Value *value);

/*
 * JSON Object
 */
/**
 * @brief Get value by name from JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return JSON_Value pointer or NULL if not found.
 */
JSON_Value *json_object_get_value(const JSON_Object *object, const char *name);
/**
 * @brief Get string property from JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return String pointer or NULL if not found.
 */
const char *json_object_get_string(const JSON_Object *object, const char *name);
/**
 * @brief Get string property length from JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return String length or 0 if not found.
 */
size_t json_object_get_string_len(
    const JSON_Object *object,
    const char *name); /* doesn't account for last null character */
/**
 * @brief Get child JSON object by name.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return JSON_Object pointer or NULL if not found.
 */
JSON_Object *json_object_get_object(const JSON_Object *object,
                                    const char *name);
/**
 * @brief Get child JSON array by name.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return JSON_Array pointer or NULL if not found.
 */
JSON_Array *json_object_get_array(const JSON_Object *object, const char *name);
/**
 * @brief Get numeric property from JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return Number value or 0 on failure.
 */
double json_object_get_number(const JSON_Object *object,
                              const char *name); /* returns 0 on fail */
/**
 * @brief Get boolean property from JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return Boolean value (1/0) or -1 on failure.
 */
int json_object_get_boolean(const JSON_Object *object,
                            const char *name); /* returns -1 on fail */

/* dotget functions enable addressing values with dot notation in nested
 objects, just like in structs or c++/java/c# objects (e.g.
 objectA.objectB.value). Because valid names in JSON can contain dots, some
 values may be inaccessible this way. */
/**
 * @brief Get value by dot notation path from JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return JSON_Value pointer or NULL if not found.
 */
JSON_Value *json_object_dotget_value(const JSON_Object *object,
                                     const char *name);
/**
 * @brief Get string property by dot notation path.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return String pointer or NULL if not found.
 */
const char *json_object_dotget_string(const JSON_Object *object,
                                      const char *name);
/**
 * @brief Get string length by dot notation path.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return String length or 0 if not found.
 */
size_t json_object_dotget_string_len(
    const JSON_Object *object,
    const char *name); /* doesn't account for last null character */
/**
 * @brief Get child object by dot notation path.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return JSON_Object pointer or NULL if not found.
 */
JSON_Object *json_object_dotget_object(const JSON_Object *object,
                                       const char *name);
/**
 * @brief Get child array by dot notation path.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return JSON_Array pointer or NULL if not found.
 */
JSON_Array *json_object_dotget_array(const JSON_Object *object,
                                     const char *name);
/**
 * @brief Get numeric property by dot notation path.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return Number value or 0 on failure.
 */
double json_object_dotget_number(const JSON_Object *object,
                                 const char *name); /* returns 0 on fail */
/**
 * @brief Get boolean property by dot notation path.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return Boolean value (1/0) or -1 on failure.
 */
int json_object_dotget_boolean(const JSON_Object *object,
                               const char *name); /* returns -1 on fail */

/* Functions to get available names */
/**
 * @brief Get number of properties in JSON object.
 *
 * @param object JSON object.
 * @return Count of properties.
 */
size_t json_object_get_count(const JSON_Object *object);
/**
 * @brief Get property name at given index.
 *
 * @param object JSON object.
 * @param index Property index.
 * @return Property name string or NULL.
 */
const char *json_object_get_name(const JSON_Object *object, size_t index);
/**
 * @brief Get value at given property index.
 *
 * @param object JSON object.
 * @param index Property index.
 * @return JSON_Value pointer or NULL.
 */
JSON_Value *json_object_get_value_at(const JSON_Object *object, size_t index);
/**
 * @brief Get wrapping JSON_Value for JSON object.
 *
 * @param object JSON object.
 * @return Parent JSON_Value pointer.
 */
JSON_Value *json_object_get_wrapping_value(const JSON_Object *object);

/* Functions to check if object has a value with a specific name. Returned value
 * is 1 if object has a value and 0 if it doesn't. dothas functions behave
 * exactly like dotget functions. */
/**
 * @brief Check if JSON object has a value with given name.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return 1 if property exists, 0 otherwise.
 */
int json_object_has_value(const JSON_Object *object, const char *name);
/**
 * @brief Check if JSON object has property of given type.
 *
 * @param object JSON object.
 * @param name Property name.
 * @param type Expected value type.
 * @return 1 if property matches type, 0 otherwise.
 */
int json_object_has_value_of_type(const JSON_Object *object, const char *name,
                                  JSON_Value_Type type);

/**
 * @brief Check if dot notation path exists in JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return 1 if path exists, 0 otherwise.
 */
int json_object_dothas_value(const JSON_Object *object, const char *name);
/**
 * @brief Check if dot notation path matches type.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @param type Expected value type.
 * @return 1 if path matches type, 0 otherwise.
 */
int json_object_dothas_value_of_type(const JSON_Object *object,
                                     const char *name, JSON_Value_Type type);

/* Creates new name-value pair or frees and replaces old value with a new one.
 * json_object_set_value does not copy passed value so it shouldn't be freed
 * afterwards. */
/**
 * @brief Set property value in JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @param value Value to set.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_set_value(JSON_Object *object, const char *name,
                                  JSON_Value *value);
/**
 * @brief Set string property in JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @param string String value.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_set_string(JSON_Object *object, const char *name,
                                   const char *string);
/**
 * @brief Set sized string property in JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @param string String value.
 * @param len String length.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_set_string_with_len(
    JSON_Object *object, const char *name, const char *string,
    size_t len); /* length shouldn't include last null character */
/**
 * @brief Set numeric property in JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @param number Number value.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_set_number(JSON_Object *object, const char *name,
                                   double number);
/**
 * @brief Set boolean property in JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @param boolean Boolean value (1/0).
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_set_boolean(JSON_Object *object, const char *name,
                                    int boolean);
/**
 * @brief Set null property in JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_set_null(JSON_Object *object, const char *name);

/* Works like dotget functions, but creates whole hierarchy if necessary.
 * json_object_dotset_value does not copy passed value so it shouldn't be freed
 * afterwards. */
/**
 * @brief Set value by dot notation path in JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @param value Value to set.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_dotset_value(JSON_Object *object, const char *name,
                                     JSON_Value *value);
/**
 * @brief Set string by dot notation path in JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @param string String value.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_dotset_string(JSON_Object *object, const char *name,
                                      const char *string);
/**
 * @brief Set sized string by dot notation path in JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @param string String value.
 * @param len String length.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_dotset_string_with_len(
    JSON_Object *object, const char *name, const char *string,
    size_t len); /* length shouldn't include last null character */
/**
 * @brief Set number by dot notation path in JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @param number Number value.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_dotset_number(JSON_Object *object, const char *name,
                                      double number);
/**
 * @brief Set boolean by dot notation path in JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @param boolean Boolean value (1/0).
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_dotset_boolean(JSON_Object *object, const char *name,
                                       int boolean);
/**
 * @brief Set null by dot notation path in JSON object.
 *
 * @param object JSON object.
 * @param name Dot notation path.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_dotset_null(JSON_Object *object, const char *name);

/* Frees and removes name-value pair */
/**
 * @brief Remove property from JSON object.
 *
 * @param object JSON object.
 * @param name Property name.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_remove(JSON_Object *object, const char *name);

/* Works like dotget function, but removes name-value pair only on exact match.
 */
/**
 * @brief Remove property by dot notation path.
 *
 * @param object JSON object.
 * @param key Dot notation path.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_dotremove(JSON_Object *object, const char *key);

/* Removes all name-value pairs in object */
/**
 * @brief Clear all properties from JSON object.
 *
 * @param object JSON object.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_object_clear(JSON_Object *object);

/*
 *JSON Array
 */
/**
 * @brief Get value at array index.
 *
 * @param array JSON array.
 * @param index Element index.
 * @return JSON_Value pointer or NULL if index out of bounds.
 */
JSON_Value *json_array_get_value(const JSON_Array *array, size_t index);
/**
 * @brief Get string at array index.
 *
 * @param array JSON array.
 * @param index Element index.
 * @return String pointer or NULL if invalid.
 */
const char *json_array_get_string(const JSON_Array *array, size_t index);
/**
 * @brief Get string length at array index.
 *
 * @param array JSON array.
 * @param index Element index.
 * @return String length or 0 if invalid.
 */
size_t json_array_get_string_len(
    const JSON_Array *array,
    size_t index); /* doesn't account for last null character */
/**
 * @brief Get child object at array index.
 *
 * @param array JSON array.
 * @param index Element index.
 * @return JSON_Object pointer or NULL if invalid.
 */
JSON_Object *json_array_get_object(const JSON_Array *array, size_t index);
/**
 * @brief Get child array at array index.
 *
 * @param array JSON array.
 * @param index Element index.
 * @return JSON_Array pointer or NULL if invalid.
 */
JSON_Array *json_array_get_array(const JSON_Array *array, size_t index);
/**
 * @brief Get number at array index.
 *
 * @param array JSON array.
 * @param index Element index.
 * @return Number value or 0 if invalid.
 */
double json_array_get_number(const JSON_Array *array,
                             size_t index); /* returns 0 on fail */
/**
 * @brief Get boolean at array index.
 *
 * @param array JSON array.
 * @param index Element index.
 * @return Boolean value (1/0) or -1 if invalid.
 */
int json_array_get_boolean(const JSON_Array *array,
                           size_t index); /* returns -1 on fail */
/**
 * @brief Get count of elements in JSON array.
 *
 * @param array JSON array.
 * @return Number of elements in array.
 */
size_t json_array_get_count(const JSON_Array *array);
/**
 * @brief Get wrapping JSON_Value for JSON array.
 *
 * @param array JSON array.
 * @return Parent JSON_Value pointer.
 */
JSON_Value *json_array_get_wrapping_value(const JSON_Array *array);

/* Frees and removes value at given index, does nothing and returns JSONFailure
 * if index doesn't exist. Order of values in array may change during execution.
 */
/**
 * @brief Remove element at index from JSON array.
 *
 * @param array JSON array.
 * @param i Index of element to remove.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_remove(JSON_Array *array, size_t i);

/* Frees and removes from array value at given index and replaces it with given
 * one. Does nothing and returns JSONFailure if index doesn't exist.
 * json_array_replace_value does not copy passed value so it shouldn't be freed
 * afterwards. */
/**
 * @brief Replace value at index in JSON array.
 *
 * @param array JSON array.
 * @param i Index to replace.
 * @param value New JSON value.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_replace_value(JSON_Array *array, size_t i,
                                     JSON_Value *value);
/**
 * @brief Replace string at index in JSON array.
 *
 * @param array JSON array.
 * @param i Index to replace.
 * @param string New string value.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_replace_string(JSON_Array *array, size_t i,
                                      const char *string);
/**
 * @brief Replace sized string at index in JSON array.
 *
 * @param array JSON array.
 * @param i Index to replace.
 * @param string New string value.
 * @param len String length.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_replace_string_with_len(
    JSON_Array *array, size_t i, const char *string,
    size_t len); /* length shouldn't include last null character */
/**
 * @brief Replace number at index in JSON array.
 *
 * @param array JSON array.
 * @param i Index to replace.
 * @param number New number value.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_replace_number(JSON_Array *array, size_t i,
                                      double number);
/**
 * @brief Replace boolean at index in JSON array.
 *
 * @param array JSON array.
 * @param i Index to replace.
 * @param boolean New boolean value (1/0).
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_replace_boolean(JSON_Array *array, size_t i,
                                       int boolean);
/**
 * @brief Replace value at index with null in JSON array.
 *
 * @param array JSON array.
 * @param i Index to replace.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_replace_null(JSON_Array *array, size_t i);

/* Frees and removes all values from array */
/**
 * @brief Clear all elements in JSON array.
 *
 * @param array JSON array.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_clear(JSON_Array *array);

/* Appends new value at the end of array.
 * json_array_append_value does not copy passed value so it shouldn't be freed
 * afterwards. */
/**
 * @brief Append value to JSON array.
 *
 * @param array JSON array.
 * @param value Value to append.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_append_value(JSON_Array *array, JSON_Value *value);
/**
 * @brief Append string to JSON array.
 *
 * @param array JSON array.
 * @param string String to append.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_append_string(JSON_Array *array, const char *string);
/**
 * @brief Append sized string to JSON array.
 *
 * @param array JSON array.
 * @param string String to append.
 * @param len String length.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_append_string_with_len(
    JSON_Array *array, const char *string,
    size_t len); /* length shouldn't include last null character */
/**
 * @brief Append number to JSON array.
 *
 * @param array JSON array.
 * @param number Number to append.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_append_number(JSON_Array *array, double number);
/**
 * @brief Append boolean to JSON array.
 *
 * @param array JSON array.
 * @param boolean Boolean to append (1/0).
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_append_boolean(JSON_Array *array, int boolean);
/**
 * @brief Append null to JSON array.
 *
 * @param array JSON array.
 * @return JSONSuccess on success or JSONFailure on failure.
 */
JSON_Status json_array_append_null(JSON_Array *array);

/*
 *JSON Value
 */
/**
 * @brief Create a new JSON object value.
 * @return New JSON_Value pointer or NULL on allocation failure.
 */
JSON_Value *json_value_init_object(void);
/**
 * @brief Create a new JSON array value.
 * @return New JSON_Value pointer or NULL on allocation failure.
 */
JSON_Value *json_value_init_array(void);
JSON_Value
    *
    /**
     * @brief Create a new JSON string value.
     *
     * @param string String contents.
     * @return New JSON_Value pointer or NULL on allocation failure.
     */
    json_value_init_string(const char *string); /* copies passed string */
/**
 * @brief Create a new JSON string value with specific length.
 *
 * @param string String contents.
 * @param length Length of string.
 * @return New JSON_Value pointer or NULL on allocation failure.
 */
JSON_Value *json_value_init_string_with_len(
    const char *string,
    size_t length); /* copies passed string, length shouldn't include last null
                       character */
/**
 * @brief Create a new JSON number value.
 *
 * @param number Numeric value.
 * @return New JSON_Value pointer or NULL on allocation failure.
 */
JSON_Value *json_value_init_number(double number);
/**
 * @brief Create a new JSON boolean value.
 *
 * @param boolean Boolean value (1/0).
 * @return New JSON_Value pointer or NULL on allocation failure.
 */
JSON_Value *json_value_init_boolean(int boolean);
/**
 * @brief Create a new JSON null value.
 * @return New JSON_Value pointer or NULL on allocation failure.
 */
JSON_Value *json_value_init_null(void);
/**
 * @brief Create a deep copy of a JSON value.
 *
 * @param value Value to clone.
 * @return Cloned JSON_Value pointer or NULL on failure.
 */
JSON_Value *json_value_deep_copy(const JSON_Value *value);
/**
 * @brief Recursively free memory allocated for JSON value.
 *
 * @param value Value to free.
 */
void json_value_free(JSON_Value *value);

/**
 * @brief Get type of JSON value.
 *
 * @param value JSON value.
 * @return Value type enum.
 */
JSON_Value_Type json_value_get_type(const JSON_Value *value);
/**
 * @brief Get wrapped JSON object pointer.
 *
 * @param value JSON value of object type.
 * @return JSON_Object pointer or NULL.
 */
JSON_Object *json_value_get_object(const JSON_Value *value);
/**
 * @brief Get wrapped JSON array pointer.
 *
 * @param value JSON value of array type.
 * @return JSON_Array pointer or NULL.
 */
JSON_Array *json_value_get_array(const JSON_Value *value);
/**
 * @brief Get wrapped string pointer.
 *
 * @param value JSON value of string type.
 * @return String pointer or NULL.
 */
const char *json_value_get_string(const JSON_Value *value);
/**
 * @brief Get wrapped string length.
 *
 * @param value JSON value of string type.
 * @return String length or 0.
 */
size_t json_value_get_string_len(
    const JSON_Value *value); /* doesn't account for last null character */
/**
 * @brief Get wrapped numeric value.
 *
 * @param value JSON value of number type.
 * @return Numeric value or 0.
 */
double json_value_get_number(const JSON_Value *value);
/**
 * @brief Get wrapped boolean value.
 *
 * @param value JSON value of boolean type.
 * @return Boolean value (1/0) or -1.
 */
int json_value_get_boolean(const JSON_Value *value);
/**
 * @brief Get parent JSON value containing this value.
 *
 * @param value Child JSON value.
 * @return Parent JSON_Value pointer or NULL.
 */
JSON_Value *json_value_get_parent(const JSON_Value *value);

/* Same as above, but shorter */
/**
 * @brief Alias for json_value_get_type.
 *
 * @param value JSON value.
 * @return Value type enum.
 */
JSON_Value_Type json_type(const JSON_Value *value);
/**
 * @brief Alias for json_value_get_object.
 *
 * @param value JSON value.
 * @return JSON_Object pointer or NULL.
 */
JSON_Object *json_object(const JSON_Value *value);
/**
 * @brief Alias for json_value_get_array.
 *
 * @param value JSON value.
 * @return JSON_Array pointer or NULL.
 */
JSON_Array *json_array(const JSON_Value *value);
/**
 * @brief Alias for json_value_get_string.
 *
 * @param value JSON value.
 * @return String pointer or NULL.
 */
const char *json_string(const JSON_Value *value);
/**
 * @brief Alias for json_value_get_string_len.
 *
 * @param value JSON value.
 * @return String length or 0.
 */
size_t json_string_len(
    const JSON_Value *value); /* doesn't account for last null character */
/**
 * @brief Alias for json_value_get_number.
 *
 * @param value JSON value.
 * @return Numeric value or 0.
 */
double json_number(const JSON_Value *value);
/**
 * @brief Alias for json_value_get_boolean.
 *
 * @param value JSON value.
 * @return Boolean value (1/0) or -1.
 */
int json_boolean(const JSON_Value *value);

#ifdef __cplusplus
}
#endif /* __cplusplus */

/** @} */

#endif

#if defined(__clang__) || defined(__GNUC__)
#endif
