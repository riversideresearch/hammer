#include "tests.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
    int total_failed = 0;

    total_failed += run_rrqwrq_tests();
    total_failed += run_data_tests();
    total_failed += run_ack_tests();
    total_failed += run_error_tests();

    if (total_failed == 0) {
        printf("All tests passed!\n");
    } else {
        printf("Failed: %d\n", total_failed);
    }

    return total_failed;
}
