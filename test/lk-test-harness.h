#ifndef LK_TEST_HARNESS_H
#define LK_TEST_HARNESS_H

/* Shared minimal test harness for the lk_test binary.  The counters
 * are defined in lk-test.c; every test file shares them so the final
 * summary covers the whole run. */

#include <stdio.h>

extern int g_tests;
extern int g_pass;
extern int g_fail;
extern int g_cur_ok;

#define BEGIN_TEST(name)                                                       \
  do {                                                                         \
    g_tests++;                                                                 \
    g_cur_ok = 1;                                                              \
    printf("  %-44s ", name);                                                  \
  } while (0)

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      if (g_cur_ok)                                                            \
        printf("FAIL\n");                                                      \
      printf("    line %d: %s\n", __LINE__, #cond);                            \
      g_cur_ok = 0;                                                            \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      if (g_cur_ok)                                                            \
        printf("FAIL\n");                                                      \
      printf("    line %d: %s == %u, expected %u\n", __LINE__, #a,             \
             (unsigned)(a), (unsigned)(b));                                    \
      g_cur_ok = 0;                                                            \
    }                                                                          \
  } while (0)

#define END_TEST()                                                             \
  do {                                                                         \
    if (g_cur_ok) {                                                            \
      printf("ok\n");                                                          \
      g_pass++;                                                                \
    } else {                                                                   \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

#endif /* LK_TEST_HARNESS_H */
