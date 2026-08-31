#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
/* Host stub for the evolution survey. Serial goes to stdout, and is
 * SILENCED by default so a 100k-case sweep does not drown in explain(). */
extern int g_serial_quiet;
struct HostSerial {
    void printf(const char *f, ...) {
        if (g_serial_quiet) return;
        va_list a; va_start(a, f); vprintf(f, a); va_end(a);
    }
    void println(const char *s = "") { if (!g_serial_quiet) printf("%s\n", s); }
    void println(double v) { if (!g_serial_quiet) printf("%f\n", v); }
};
extern HostSerial Serial;
long random(long lo, long hi);
long random(long hi);
