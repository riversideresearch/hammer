#include "dns.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    uint8_t input[1024] = {0};
    FILE *f_input = stdin;

    if (argc > 1) {
        f_input = fopen(argv[1], "rb");
        if (!f_input) {
            perror("Failed to open file");
            return 1;
        }
    } else {
        puts("Reading from stdin");
    }

    size_t input_size = fread(input, 1, sizeof(input), f_input);
    if (f_input != stdin)
        fclose(f_input);

    DNSContext ctx = {0};
    ctx.packet_len = input_size;

    // Header
    HParser *header = headerParser(&ctx);
    HParseResult *header_result = h_parse(header, input, input_size);

    if (!header_result) {
        puts("Bad header, Packet Failed");
        return 1;
    }

    // Body
    if (input_size < 12) {
        puts("Bad body, Packet Failed");
        h_parse_result_free(header_result);
        return 1;
    }

    HParser *body = bodyParser(&ctx);
    HParseResult *body_result = h_parse(body, input + 12, input_size - 12);

    if (!body_result) {
        puts("Bad body, Packet Failed");
        h_parse_result_free(header_result);
        return 1;
    }

    puts("Packet Passed");
    h_parse_result_free(header_result);
    h_parse_result_free(body_result);
    return 0;
}
