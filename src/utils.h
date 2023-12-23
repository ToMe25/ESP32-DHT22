/*
 * utils.h
 *
 * This file contains some general utilities that didn't fit any other file.
 *
 *  Created on: Sep 16, 2023
 *
 * Copyright (C) 2023 ToMe25.
 * This project is licensed under the MIT License.
 * The MIT license can be found in the project root and at https://opensource.org/licenses/MIT.
 */

#ifndef SRC_UTILS_H_
#define SRC_UTILS_H_

#include <string>

namespace utility {
/**
 * A method to get the index of the Most Significant Bit set in a number.
 *
 * @param number	The number to get the MSB for.
 * @return	The index of the Most Significant Bit set in the number.
 */
uint8_t get_msb(const uint32_t number);

/**
 * A constexpr calculating the offset of the last directory separator in a string from the string start.
 *
 * @tparam S	The size of the character array to handle.
 * @param file	The string to calculate the offset for.
 * @param i		The current index in the string to be checked.
 * @return	The calculated start index of the base name.
 */
template<size_t S>
constexpr size_t get_base_name_offset(const char (&file)[S],
		const size_t i = S - 1) {
	return (file[i] == '/' || file[i] == '\\') ?
			i + 1 : (i > 0 ? get_base_name_offset(file, i - 1) : 0);
}

/**
 * A helper struct holding a constexpr value of a given type.
 * @tparam T	The type of the object to store.
 * @tparam v	The value to store.
 */
template<typename T, T v>
struct const_expr_value {
	static constexpr const T value = v;
};

/**
 * Determines the length of the given string as a constant expression.
 *
 * @param str	The string to get the length of.
 * @return	The number of characters in the given string.
 */
constexpr size_t strlen(const char *str) {
	return (*str == 0) ? 0 : strlen(str + 1) + 1;
}
}

/**
 * CONST_EXPR_VALUE is an utility macro to get force compile time evaluation of a constexpr value.
 */
#define CONST_EXPR_VALUE(exp) utility::const_expr_value<decltype(exp), exp>::value

/**
 * FILE_BASE_NAME is a macro that represents the base name of the current file.
 */
#ifdef __FILE_NAME__
#define FILE_BASE_NAME __FILE_NAME__
#else
#define FILE_BASE_NAME &__FILE__[CONST_EXPR_VALUE(utility::get_base_name_offset(__FILE__))]
#endif

/**
 * EXPAND_MACRO is a utility macro to expand a macro to its string form.
 */
#define EXPAND_MACRO(macro) #macro

namespace utility {
/**
 * Converts the given temperature from degrees celsius to degrees fahrenheit.
 *
 * @param celsius	The temperature to convert in celsius.
 * @return	The converted temperature in fahrenheit.
 */
float celsiusToFahrenheit(const float celsius);

/**
 * Converts the given floating point number to a string.
 *
 * Returns "Unknown" if the measurement is NAN.
 *
 * Necessary, in part, because `std::to_string(float)` doesn't exist in the GCC version used by the espressif framework.
 * Also because `std::to_string(float)` doesn't allow specifying the number of decimal digits.
 *
 * @param measurement		The floating point number to convert to a string.
 * @param decimal_digits	The number of digits after the decimal dot to round to.
 * @return	The newly created string.
 */
std::string float_to_string(const float measurement,
		const uint8_t decimal_digits);

/**
 * Converts the given timespan to a string.
 *
 * Converts the given timespan in milliseconds to a string with the format "HH:MM:SS.mmm".
 * If the value is more than 100 hours, `time_ms % 100h` is converted to a string.
 *
 * If the value is negative "Unknown" will be returned.
 *
 * @param time_ms	The timespan to convert to a string.
 * @return	The newly created string.
 */
std::string timespan_to_string(const int64_t time_ms);
}

#endif /* SRC_UTILS_H_ */
