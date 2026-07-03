#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "ASSERT TRUE failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define TEST_ASSERT_INT_EQ(expected, actual) \
    do { \
        int _expected = (expected); \
        int _actual = (actual); \
        if (_expected != _actual) { \
            fprintf(stderr, "ASSERT INT EQ failed: expected=%d actual=%d (%s:%d)\n", _expected, _actual, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define TEST_ASSERT_STR_EQ(expected, actual) \
    do { \
        const char *_expected = (expected); \
        const char *_actual = (actual); \
        if (strcmp(_expected, _actual) != 0) { \
            fprintf(stderr, "ASSERT STR EQ failed: expected=\"%s\" actual=\"%s\" (%s:%d)\n", _expected, _actual, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#endif
