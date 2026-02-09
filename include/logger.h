#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>

/* Log Level Enumeration */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_CRITICAL
} log_level_t;

/* Logger Configuration */
typedef struct {
    FILE *output_file;          /* Output file pointer */
    log_level_t min_level;      /* Minimum log level to output */
    int use_colors;             /* Enable colored output */
    int use_timestamps;         /* Include timestamps in logs */
} logger_config_t;

/* Function Declarations */
void logger_init(const char *log_file, log_level_t min_level);
void logger_cleanup(void);
void logger_log(log_level_t level, const char *format, ...);
void logger_debug(const char *format, ...);
void logger_info(const char *format, ...);
void logger_warn(const char *format, ...);
void logger_error(const char *format, ...);
void logger_critical(const char *format, ...);
void logger_hexdump(const char *label, const uint8_t *data, size_t length);

#endif /* LOGGER_H */
