/* Parser combinators for binary formats.
 * Copyright (c) 2025 Riverside Research
 * Copyright (C) 2012  Meredith L. Patterson, Dan "TQ" Hirsch
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef HAMMER_HAMMER_AUTO_SOURCE__H
#define HAMMER_HAMMER_AUTO_SOURCE__H

#include "hammer.h"

#ifdef __cplusplus
extern "C" {
#endif

HParser *h_parser_auto_source_at(HParser *parser, const char *file, const char *function,
                                 size_t line, size_t column);

#define H_AUTO_SOURCE_CALL(name, ...)                                                              \
    h_parser_auto_source_at((name)(__VA_ARGS__), __FILE__, __func__, __LINE__, H_CONTEXT_COLUMN())

#define h_ch(...) H_AUTO_SOURCE_CALL(h_ch, __VA_ARGS__)
#define h_token(...) H_AUTO_SOURCE_CALL(h_token, __VA_ARGS__)
#define h_sequence(...) H_AUTO_SOURCE_CALL(h_sequence, __VA_ARGS__)
#define h_choice(...) H_AUTO_SOURCE_CALL(h_choice, __VA_ARGS__)
#define h_attr_bool(...) H_AUTO_SOURCE_CALL(h_attr_bool, __VA_ARGS__)
#define h_bits(...) H_AUTO_SOURCE_CALL(h_bits, __VA_ARGS__)
#define h_uint8(...) H_AUTO_SOURCE_CALL(h_uint8, __VA_ARGS__)
#define h_uint16(...) H_AUTO_SOURCE_CALL(h_uint16, __VA_ARGS__)
#define h_uint32(...) H_AUTO_SOURCE_CALL(h_uint32, __VA_ARGS__)
#define h_uint64(...) H_AUTO_SOURCE_CALL(h_uint64, __VA_ARGS__)
#define h_int8(...) H_AUTO_SOURCE_CALL(h_int8, __VA_ARGS__)
#define h_int16(...) H_AUTO_SOURCE_CALL(h_int16, __VA_ARGS__)
#define h_int32(...) H_AUTO_SOURCE_CALL(h_int32, __VA_ARGS__)
#define h_int64(...) H_AUTO_SOURCE_CALL(h_int64, __VA_ARGS__)
#define h_bytes(...) H_AUTO_SOURCE_CALL(h_bytes, __VA_ARGS__)
#define h_many(...) H_AUTO_SOURCE_CALL(h_many, __VA_ARGS__)
#define h_many1(...) H_AUTO_SOURCE_CALL(h_many1, __VA_ARGS__)
#define h_many_cap(...) H_AUTO_SOURCE_CALL(h_many_cap, __VA_ARGS__)
#define h_many1_cap(...) H_AUTO_SOURCE_CALL(h_many1_cap, __VA_ARGS__)
#define h_repeat_n(...) H_AUTO_SOURCE_CALL(h_repeat_n, __VA_ARGS__)
#define h_sepBy1(...) H_AUTO_SOURCE_CALL(h_sepBy1, __VA_ARGS__)
#define h_optional(...) H_AUTO_SOURCE_CALL(h_optional, __VA_ARGS__)
#define h_int_range(...) H_AUTO_SOURCE_CALL(h_int_range, __VA_ARGS__)
#define h_permutation(...) H_AUTO_SOURCE_CALL(h_permutation, __VA_ARGS__)
#define h_butnot(...) H_AUTO_SOURCE_CALL(h_butnot, __VA_ARGS__)
#define h_difference(...) H_AUTO_SOURCE_CALL(h_difference, __VA_ARGS__)
#define h_xor(...) H_AUTO_SOURCE_CALL(h_xor, __VA_ARGS__)
#define h_left(...) H_AUTO_SOURCE_CALL(h_left, __VA_ARGS__)
#define h_right(...) H_AUTO_SOURCE_CALL(h_right, __VA_ARGS__)
#define h_middle(...) H_AUTO_SOURCE_CALL(h_middle, __VA_ARGS__)
#define h_ignore(...) H_AUTO_SOURCE_CALL(h_ignore, __VA_ARGS__)
#define h_whitespace(...) H_AUTO_SOURCE_CALL(h_whitespace, __VA_ARGS__)
#define h_drop_from_(...) H_AUTO_SOURCE_CALL(h_drop_from_, __VA_ARGS__)
#define h_ch_range(...) H_AUTO_SOURCE_CALL(h_ch_range, __VA_ARGS__)
#define h_in(...) H_AUTO_SOURCE_CALL(h_in, __VA_ARGS__)
#define h_not_in(...) H_AUTO_SOURCE_CALL(h_not_in, __VA_ARGS__)
#define h_and(...) H_AUTO_SOURCE_CALL(h_and, __VA_ARGS__)
#define h_not(...) H_AUTO_SOURCE_CALL(h_not, __VA_ARGS__)
#define h_end_p(...) H_AUTO_SOURCE_CALL(h_end_p, __VA_ARGS__)
#define h_epsilon_p(...) H_AUTO_SOURCE_CALL(h_epsilon_p, __VA_ARGS__)
#define h_nothing_p(...) H_AUTO_SOURCE_CALL(h_nothing_p, __VA_ARGS__)
#define h_put_value(...) H_AUTO_SOURCE_CALL(h_put_value, __VA_ARGS__)
#define h_get_value(...) H_AUTO_SOURCE_CALL(h_get_value, __VA_ARGS__)
#define h_free_value(...) H_AUTO_SOURCE_CALL(h_free_value, __VA_ARGS__)
#define h_action(...) H_AUTO_SOURCE_CALL(h_action, __VA_ARGS__)
#define h_bind(...) H_AUTO_SOURCE_CALL(h_bind, __VA_ARGS__)
#define h_indirect(...) H_AUTO_SOURCE_CALL(h_indirect, __VA_ARGS__)
#define h_skip(...) H_AUTO_SOURCE_CALL(h_skip, __VA_ARGS__)
#define h_seek(...) H_AUTO_SOURCE_CALL(h_seek, __VA_ARGS__)
#define h_with_endianness(...) H_AUTO_SOURCE_CALL(h_with_endianness, __VA_ARGS__)
#define h_action_stash(...) H_AUTO_SOURCE_CALL(h_action_stash, __VA_ARGS__)
#define h_action_apply(...) H_AUTO_SOURCE_CALL(h_action_apply, __VA_ARGS__)
/* hammer.h defines h_dispatch as an array-counting convenience macro. Preserve
 * that behavior while adding the source annotation. */
#undef h_dispatch
#define h_dispatch(discriminator, map, default_parser)                                             \
    h_parser_auto_source_at(                                                                       \
        h_dispatch__s((discriminator), (map), (sizeof(map) / sizeof((map)[0])), (default_parser)), \
        __FILE__, __func__, __LINE__, H_CONTEXT_COLUMN())
#define h_float_range(...) H_AUTO_SOURCE_CALL(h_float_range, __VA_ARGS__)
#define h_float16(...) H_AUTO_SOURCE_CALL(h_float16, __VA_ARGS__)
#define h_float32(...) H_AUTO_SOURCE_CALL(h_float32, __VA_ARGS__)
#define h_float64(...) H_AUTO_SOURCE_CALL(h_float64, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // HAMMER_HAMMER_AUTO_SOURCE__H
