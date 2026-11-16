/**
 * @file
 * @brief Min and max macros.
 */

#pragma once

/** @note Arguments are evaluated twice: no side effects. */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/** @brief MIN/MAX after casting both sides to @p type (avoids sign mixing). */
#define min_t(type, a, b) MIN((type)(a), (type)(b))
#define max_t(type, a, b) MAX((type)(a), (type)(b))
