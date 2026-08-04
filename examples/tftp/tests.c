#include "tests.h"

#include "tftp.h"

#include <stdint.h>
#include <stdio.h>

int run_rrqwrq_tests(void) {
    HParser *parser = rrqwrqParser();
    int passed = 0;
    int failed = 0;

    // Test 1: normal write request (should pass)
    uint8_t rwTest1[] = "\x00\x02\x6d\x79\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x00\x6f\x63"
                        "\x74\x65\x74\x00";
    HParseResult *result1 = h_parse(parser, rwTest1, sizeof(rwTest1) - 1);
    if (result1 != NULL) {
        printf("RRQ/WRQ Test 1 (normal write request): PASSED ✓\n");
        passed++;
    } else {
        printf("RRQ/WRQ Test 1 (normal write request): FAILED (expected to pass)\n");
        failed++;
    }

    // Test 2: normal read request (should pass)
    uint8_t rwTest2[] = "\x00\x01\x6d\x79\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x00\x6f\x63"
                        "\x74\x65\x74\x00";
    HParseResult *result2 = h_parse(parser, rwTest2, sizeof(rwTest2) - 1);
    if (result2 != NULL) {
        printf("RRQ/WRQ Test 2 (normal read request): PASSED ✓\n");
        passed++;
    } else {
        printf("RRQ/WRQ Test 2 (normal read request): FAILED (expected to pass)\n");
        failed++;
    }

    // Test 3: invalid packet op code (should fail)
    uint8_t rwTest3[] = "\x00\x6d\x79\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x00\x6f\x63\x74"
                        "\x65\x74\x00";
    HParseResult *result3 = h_parse(parser, rwTest3, sizeof(rwTest3) - 1);
    if (result3 == NULL) {
        printf("RRQ/WRQ Test 3 (invalid op code): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("RRQ/WRQ Test 3 (invalid op code): FAILED (expected to reject)\n");
        failed++;
    }

    // Test 4: invalid filename terminator (should fail)
    uint8_t rwTest4[] = "\x00\x02\x6d\x79\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63"
                        "\x74\x65\x74\x00";
    HParseResult *result4 = h_parse(parser, rwTest4, sizeof(rwTest4) - 1);
    if (result4 == NULL) {
        printf("RRQ/WRQ Test 4 (invalid filename terminator): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("RRQ/WRQ Test 4 (invalid filename terminator): FAILED (expected to reject)\n");
        failed++;
    }
    printf("\n");

    return failed;
}

int run_data_tests(void) {
    HParser *parser = dataParser();
    int passed = 0;
    int failed = 0;

    // Test 1: passes
    uint8_t dataPTest1[] = "\x00\x03\xff\xff\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f"
                           "\x63\x74\x65\x74\x00";
    HParseResult *result1 = h_parse(parser, dataPTest1, sizeof(dataPTest1) - 1);
    if (result1 != NULL) {
        printf("DATA Test 1 (valid data packet): PASSED ✓\n");
        passed++;
    } else {
        printf("DATA Test 1 (valid data packet): FAILED (expected to pass)\n");
        failed++;
    }

    // Test 2: passes
    uint8_t dataPTest2[] = "\x00\x03\x00\x01\x78\x74\x71\x6f\x63\x74\x65\x74\x06";
    HParseResult *result2 = h_parse(parser, dataPTest2, sizeof(dataPTest2) - 1);
    if (result2 != NULL) {
        printf("DATA Test 2 (valid data packet): PASSED ✓\n");
        passed++;
    } else {
        printf("DATA Test 2 (valid data packet): FAILED (expected to pass)\n");
        failed++;
    }

    // Test 3: passes
    uint8_t dataPTest3[] = "\x00\x03\x01\x30\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f"
                           "\x63\x74\x65\x74\x00"
                           "\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00";
    HParseResult *result3 = h_parse(parser, dataPTest3, sizeof(dataPTest3) - 1);
    if (result3 != NULL) {
        printf("DATA Test 3 (valid data packet): PASSED ✓\n");
        passed++;
    } else {
        printf("DATA Test 3 (valid data packet): FAILED (expected to pass)\n");
        failed++;
    }

    // Test 4: invalid, message range not >1 (should fail)
    uint8_t dataPTest4[] = "\x00\x03\x00\x00\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f"
                           "\x63\x74\x65\x74\x00";
    HParseResult *result4 = h_parse(parser, dataPTest4, sizeof(dataPTest4) - 1);
    if (result4 == NULL) {
        printf("DATA Test 4 (invalid message range): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("DATA Test 4 (invalid message range): FAILED (expected to reject)\n");
        failed++;
    }

    // Test 5: valid, max length packet (should pass)
    uint8_t dataPTest5[] =
        "\x00\x03\x00\x01\x5f\x63\x6f\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00"
        "\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78"
        "\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74"
        "\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74"
        "\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65"
        "\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e"
        "\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74"
        "\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67"
        "\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63"
        "\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69"
        "\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f"
        "\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66"
        "\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01"
        "\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e"
        "\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74"
        "\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00"
        "\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78"
        "\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74"
        "\x00\x6e\x66\x69\x67\x2e\x74\x78\x74\x01\x6f\x63\x74\x65\x74\x00\x6e\x66\x69\x67\x2e\x74"
        "\x78\x74\x01\x01\x01\x01";
    HParseResult *result5 = h_parse(parser, dataPTest5, sizeof(dataPTest5) - 1);
    if (result5 != NULL) {
        printf("DATA Test 5 (max length packet): PASSED ✓\n");
        passed++;
    } else {
        printf("DATA Test 5 (max length packet): FAILED (expected to pass)\n");
        failed++;
    }
    printf("\n");

    return failed;
}

int run_ack_tests(void) {
    HParser *parser = ackParser();
    int passed = 0;
    int failed = 0;

    // Test 1: valid packet within range (should pass)
    uint8_t ackPTest1[] = "\x00\x04\x00\x01";
    HParseResult *result1 = h_parse(parser, ackPTest1, sizeof(ackPTest1) - 1);
    if (result1 != NULL) {
        printf("ACK Test 1 (valid packet): PASSED ✓\n");
        passed++;
    } else {
        printf("ACK Test 1 (valid packet): FAILED (expected to pass)\n");
        failed++;
    }

    // Test 2: invalid packet, too long (should fail)
    uint8_t ackPTest2[] = "\x00\x04\x5f\x63\x6f";
    HParseResult *result2 = h_parse(parser, ackPTest2, sizeof(ackPTest2) - 1);
    if (result2 == NULL) {
        printf("ACK Test 2 (packet too long): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("ACK Test 2 (packet too long): FAILED (expected to reject)\n");
        failed++;
    }

    // Test 3: invalid packet, too long (should fail)
    uint8_t ackPTest3[] = "\x00\x00\x04\x00\x00";
    HParseResult *result3 = h_parse(parser, ackPTest3, sizeof(ackPTest3) - 1);
    if (result3 == NULL) {
        printf("ACK Test 3 (packet too long): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("ACK Test 3 (packet too long): FAILED (expected to reject)\n");
        failed++;
    }

    // Test 4: invalid packet, op code (should fail)
    uint8_t ackPTest4[] = "\x00\x03\x00\x00";
    HParseResult *result4 = h_parse(parser, ackPTest4, sizeof(ackPTest4) - 1);
    if (result4 == NULL) {
        printf("ACK Test 4 (invalid op code): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("ACK Test 4 (invalid op code): FAILED (expected to reject)\n");
        failed++;
    }
    printf("\n");

    return failed;
}

int run_error_tests(void) {
    HParser *parser = errpktParser();
    int passed = 0;
    int failed = 0;

    // Test 1: valid packet within range (should pass)
    uint8_t errTest1[] = "\x00\x05\x00\x01\x6f\x63\x74\x65\x74\x00";
    HParseResult *result1 = h_parse(parser, errTest1, sizeof(errTest1) - 1);
    if (result1 != NULL) {
        printf("ERROR Test 1 (valid error packet): PASSED ✓\n");
        passed++;
    } else {
        printf("ERROR Test 1 (valid error packet): FAILED (expected to pass)\n");
        failed++;
    }

    // Test 2: invalid packet, err code out of range (should fail)
    uint8_t errTest2[] = "\x00\x05\x00\x0f\x6f\x63\x74\x65\x74\x00";
    HParseResult *result2 = h_parse(parser, errTest2, sizeof(errTest2) - 1);
    if (result2 == NULL) {
        printf("ERROR Test 2 (err code out of range): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("ERROR Test 2 (err code out of range): FAILED (expected to reject)\n");
        failed++;
    }

    // Test 3: invalid packet, contains invalid string char (should fail)
    uint8_t errTest3[] = "\x00\x05\x04\x00\x1b\x2b";
    HParseResult *result3 = h_parse(parser, errTest3, sizeof(errTest3) - 1);
    if (result3 == NULL) {
        printf("ERROR Test 3 (invalid string char): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("ERROR Test 3 (invalid string char): FAILED (expected to reject)\n");
        failed++;
    }

    // Test 4: invalid packet, no trailing zero (should fail)
    uint8_t errTest4[] = "\x00\x05\x00\x01\x6f\x63\x74\x65\x74\x01";
    HParseResult *result4 = h_parse(parser, errTest4, sizeof(errTest4) - 1);
    if (result4 == NULL) {
        printf("ERROR Test 4 (no trailing zero): PASSED ✓ (correctly rejected)\n");
        passed++;
    } else {
        printf("ERROR Test 4 (no trailing zero): FAILED (expected to reject)\n");
        failed++;
    }
    printf("\n");

    return failed;
}
