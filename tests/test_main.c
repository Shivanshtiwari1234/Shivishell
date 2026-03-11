#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "parse.h"
#include "history.h"

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a); \
    int _b = (b); \
    if (_a != _b) { \
        fprintf(stderr, "ASSERT_EQ_INT failed: %d != %d (%s:%d)\n", _a, _b, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define ASSERT_STREQ(a, b) do { \
    const char *_a = (a); \
    const char *_b = (b); \
    if (strcmp(_a, _b) != 0) { \
        fprintf(stderr, "ASSERT_STREQ failed: '%s' != '%s' (%s:%d)\n", _a, _b, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static int test_split_pipeline_basic(void) {
    char buf[] = "echo hi|wc";
    char *segs[4];
    int n = split_pipeline_inplace(buf, segs, 4);
    ASSERT_EQ_INT(n, 2);
    ASSERT_STREQ(segs[0], "echo hi");
    ASSERT_STREQ(segs[1], "wc");
    return 0;
}

static int test_split_pipeline_spaces(void) {
    char buf[] = "  ls  |  grep a  ";
    char *segs[4];
    int n = split_pipeline_inplace(buf, segs, 4);
    ASSERT_EQ_INT(n, 2);
    ASSERT_STREQ(segs[0], "ls");
    ASSERT_STREQ(segs[1], "grep a");
    return 0;
}

static int test_split_pipeline_quotes(void) {
    char buf[] = "echo 'a|b'|cat";
    char *segs[4];
    int n = split_pipeline_inplace(buf, segs, 4);
    ASSERT_EQ_INT(n, 2);
    ASSERT_STREQ(segs[0], "echo 'a|b'");
    ASSERT_STREQ(segs[1], "cat");
    return 0;
}

static int test_parse_args_quotes(void) {
    char buf[] = "echo \"a b\" c";
    char *argv[8];
    int argc = parse_args_inplace(buf, argv, 8);
    ASSERT_EQ_INT(argc, 3);
    ASSERT_STREQ(argv[0], "echo");
    ASSERT_STREQ(argv[1], "a b");
    ASSERT_STREQ(argv[2], "c");
    return 0;
}

static int test_parse_args_single_quotes(void) {
    char buf[] = "cmd 'x y'";
    char *argv[8];
    int argc = parse_args_inplace(buf, argv, 8);
    ASSERT_EQ_INT(argc, 2);
    ASSERT_STREQ(argv[0], "cmd");
    ASSERT_STREQ(argv[1], "x y");
    return 0;
}

static int test_history_add(void) {
    history_count = 0;
    history_add("a");
    history_add("a");
    history_add("b");
    ASSERT_EQ_INT(history_count, 2);
    ASSERT_STREQ(history_buf[0], "a");
    ASSERT_STREQ(history_buf[1], "b");
    return 0;
}

static int test_history_overflow(void) {
    history_count = 0;
    char line[32];
    for (int i = 0; i < HISTORY_SIZE + 5; ++i) {
        snprintf(line, sizeof(line), "cmd_%d", i);
        history_add(line);
    }
    ASSERT_EQ_INT(history_count, HISTORY_SIZE);
    snprintf(line, sizeof(line), "cmd_%d", HISTORY_SIZE + 4);
    ASSERT_STREQ(history_buf[HISTORY_SIZE - 1], line);
    return 0;
}

int main(void) {
    if (test_split_pipeline_basic() != 0) return 1;
    if (test_split_pipeline_spaces() != 0) return 1;
    if (test_split_pipeline_quotes() != 0) return 1;
    if (test_parse_args_quotes() != 0) return 1;
    if (test_parse_args_single_quotes() != 0) return 1;
    if (test_history_add() != 0) return 1;
    if (test_history_overflow() != 0) return 1;

    printf("All tests passed.\n");
    return 0;
}
