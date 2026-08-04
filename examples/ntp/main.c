#include "hex.c"
#include "ntp.c"

int main(int argc, char *argv[]) {
    // Large buffer for input (careful: stack space!)
    uint8_t input[1024] = {0};
    FILE *f_input = stdin;

    if (argc > 1) {
        f_input = fopen(argv[1], "rb");
        if (!f_input) {
            puts("ERR: cannot open file");
            return 1;
        }
    } else {
        puts("Reading from stdin");
    }

    // Read input from stdin
    size_t input_size = fread(input, 1, sizeof(input), f_input);

    // Run the actual NTP packet parser
    HParseResult *result = h_parse(ntpParser(), input, input_size);

    if (result) {
        printf("Packet accepted\n");
        return 0;
    } else {
        printf("Packet rejected\n");
        return -1;
    }
}
