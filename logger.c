/**
 * This file implements the Logging API specified in logger.h.
 */

#include "logger.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static enum log_level_e current_log_level = LOG_INFO; ///< Global variable defining the current log level.
static unsigned long LOG_COUNT = 1;                   ///< Global variable to keep count of the logs

void set_log_level(const enum log_level_e log_level) {
    assert(log_level >= LOG_NONE);

    current_log_level = log_level;
}

void log_message(const enum log_level_e log_level, const char *const message) {
    assert(log_level > LOG_NONE);
    assert(message != NULL);
    if (log_level > current_log_level) {
        return;
    }

    (void)fprintf(stderr, "[%lu]: %s\n", LOG_COUNT, message);
    LOG_COUNT++;
}

void log_printf(const enum log_level_e log_level, const char *const format, ...) {
    assert(log_level > LOG_NONE);
    assert(format != NULL);

    if (log_level > current_log_level) {
        return;
    }

    va_list ap;

    va_start(ap, format);
    fprintf(stderr, "[%lu]: ", LOG_COUNT);
    (void)vfprintf(stderr, format, ap);
    va_end(ap);

    (void)fputs("\n", stderr);
    LOG_COUNT++;
}
