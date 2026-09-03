#ifndef __RTS_TEST_H__
#define __RTS_TEST_H__

#include <stdio.h>

static inline int rts_fail(const char *tag, const char *message) {
    if (tag && tag[0] != '\0') {
        fprintf(stderr, "FAIL (%s): %s\n", tag, message);
    } else {
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return 1;
}

#define RTS_CHECK(cond, tag, msg) \
    do { if (!(cond)) return rts_fail((tag), (msg)); } while (0)

#define RTS_RUN(fn) \
    do { int _r = (fn); if (_r != 0) return _r; } while (0)

#endif
