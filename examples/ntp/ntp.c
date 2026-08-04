#include <hammer/glue.h>
#include <hammer/hammer.h>
#include <string.h>

HParsedToken *opt_ext_len(const HParseResult *p, void *user_data) {
    unsigned int temp = p->ast->uint;
    return H_MAKE_UINT(temp - 4);
}

HParser *ntpParser() {
    // Header
    H_RULE(leap, h_int_range(h_bits(2, false), 0, 3));    // 2 bits, must be between 0-3
    H_RULE(version, h_int_range(h_bits(3, false), 1, 4)); // 3 bits, must be between 1-4
    H_RULE(mode, h_int_range(h_bits(3, false), 0, 7));    // 3 bits, must be between 0-7
    H_RULE(stratum, h_int_range(h_uint8(), 0, 16));       // 8 bits, must be between 0-16
    H_RULE(poll, h_int8());                               // 8 bits
    H_RULE(precision, h_int8());                          // 8 bits

    H_RULE(header, h_sequence(leap, version, mode, stratum, poll, precision, NULL));

    // Essential fields
    H_RULE(root_delay, h_sequence(h_int16(), h_int16(), NULL)); // 32 bits
    H_RULE(root_disp, h_sequence(h_int16(), h_int16(), NULL));  // 32 bits
    H_RULE(ref_id, h_uint32());                                 // 32 bits
    H_RULE(ref_ts, h_sequence(h_uint32(), h_uint32(), NULL));   // 64 bits
    H_RULE(org_ts, h_sequence(h_uint32(), h_uint32(), NULL));   // 64 bits
    H_RULE(rec_ts, h_sequence(h_uint32(), h_uint32(), NULL));   // 64 bits
    H_RULE(xmt_ts, h_sequence(h_uint32(), h_uint32(), NULL));   // 64 bits

    H_RULE(essential_fields,
           h_sequence(header, root_delay, root_disp, ref_id, ref_ts, org_ts, rec_ts, xmt_ts, NULL));

    // Optional extension fields; can be repeated multiple times per packet
    H_RULE(field_type, h_int16()); // 16 bits
    H_RULE(ext_field,
           h_sequence(
               field_type, h_put_value(h_uint16(), "opt_len_val"),
               h_length_value(h_action(h_free_value("opt_len_val"), opt_ext_len, NULL), h_uint8()),
               NULL));

    H_RULE(ext_fields, h_many(ext_field));

    // Optional MAC (message authentication code)
    H_RULE(key_id, h_uint32());                           // 32 bits
    H_RULE(dgst, h_sequence(h_int64(), h_int64(), NULL)); // 128 bits
    H_RULE(mac, h_sequence(key_id, dgst, NULL));

    // Combinations of optional fields to get every type of possible NTP packet
    H_RULE(type1, h_sequence(essential_fields, ext_fields, h_end_p(), NULL));
    H_RULE(type2, h_sequence(essential_fields, ext_fields, mac, h_end_p(), NULL));

    // Matches the first parser that succeeds in sequential order
    HParser *ntp = h_left(h_choice(type1, type2, NULL), h_end_p());

    // Return the parser
    return ntp;
}