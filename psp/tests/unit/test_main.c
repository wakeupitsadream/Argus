#include "minitest.h"
#include "tests.h"

MT_DEFINE_COUNTERS();

int main(void) {
    tests_core();
    tests_font();
    tests_i18n();
    tests_camera();
    tests_level();
    tests_save();
    return MT_SUMMARY();
}
