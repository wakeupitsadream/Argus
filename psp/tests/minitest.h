/* minitest.h — минимальный тест-фреймворк без зависимостей. */
#ifndef MINITEST_H
#define MINITEST_H
#include <stdio.h>
#include <string.h>
#include <math.h>

static int mt_checks = 0, mt_fails = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  %-40s", #name); int before = mt_fails; name(); \
    puts(before == mt_fails ? "ok" : "FAIL"); } while (0)
#define CHECK(cond) do { mt_checks++; if (!(cond)) { mt_fails++; \
    printf("\n    %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { mt_checks++; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { mt_fails++; printf("\n    %s:%d: %s == %s (%lld != %lld)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { mt_checks++; double _a = (a), _b = (b); \
    if (fabs(_a - _b) > (eps)) { mt_fails++; printf("\n    %s:%d: %s ~ %s (%g vs %g)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { mt_checks++; if (strcmp((a), (b)) != 0) { mt_fails++; \
    printf("\n    %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)
#define MT_SUMMARY() (printf("%d checks, %d failures\n", mt_checks, mt_fails), mt_fails ? 1 : 0)

#endif
