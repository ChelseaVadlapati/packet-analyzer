#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h"

static logger_config_t *global_logger = NULL;

static const char *log_level_to_string(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO: return "INFO";
        case LOG_WARN: return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

static const char *get_color_code(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "\033[36m";      /* Cyan */
        case LOG_INFO: return "\033[32m";       /* Green */
        case LOG_WARN: return "\033[33m";       /* Yellow */
        case LOG_ERROR: return "\033[31m";      /* Red */
        case LOG_CRITICAL: return "\033[35m";   /* Magenta */
        default: return "\033[0m";              /* Reset */
    }
}

void logger_init(const char *log_file, log_level_t min_level) {
    if (global_logger != NULL) {
        logger_cleanup();
    }

    global_logger = (logger_config_t *)malloc(sizeof(logger_config_t));
    if (global_logger == NULL) {
        fprintf(stderr, "Failed to allocate memory for logger\n");
        return;
    }

    if (log_file != NULL) {
        global_logger->output_file = fopen(log_file, "a");
        if (global_logger->output_file == NULL) {
            fprintf(stderr, "Failed to open log file: %s\n", log_file);
            global_logger->output_file = stdout;
        }
    } else {
        global_logger->output_file = stdout;
    }

    global_logger->min_level = min_level;
    global_logger->use_colors = 1;
    global_logger->use_timestamps = 1;

    logger_info("Logger initialized");
}

void logger_cleanup(void) {
    if (global_logger == NULL) return;

    if (global_logger->output_file != NULL && global_logger->output_file != stdout) {
        fclose(global_logger->output_file);
    }

    free(global_logger);
    global_logger = NULL;
}

void logger_log(log_level_t level, const char *format, ...) {
    if (global_logger == NULL) {
        logger_init(NULL, LOG_INFO);
    }

    if (level < global_logger->min_level) {
        return;
    }

    va_list args;
    va_start(args, format);

    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    FILE *out = global_logger->output_file;
    const char *level_str = log_level_to_string(level);

    if (global_logger->use_colors && out == stdout) {
        fprintf(out, "%s", get_color_code(level));
    }

    if (global_logger->use_timestamps) {
        fprintf(out, "[%s] ", timestamp);
    }

    fprintf(out, "[%s] ", level_str);
    vfprintf(out, format, args);
    fprintf(out, "\n");

    if (global_logger->use_colors && out == stdout) {
        fprintf(out, "\033[0m");
    }

    fflush(out);
    va_end(args);
}

void logger_debug(const char *format, ...) {
    if (global_logger == NULL || global_logger->min_level > LOG_DEBUG) return;
    
    va_list args;
    va_start(args, format);
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    FILE *out = global_logger->output_file;
    if (global_logger->use_colors && out == stdout) fprintf(out, "%s", get_color_code(LOG_DEBUG));
    if (global_logger->use_timestamps) fprintf(out, "[%s] ", timestamp);
    fprintf(out, "[DEBUG] ");
    vfprintf(out, format, args);
    fprintf(out, "\n");
    if (global_logger->use_colors && out == stdout) fprintf(out, "\033[0m");
    fflush(out);
    va_end(args);
}

void logger_info(const char *format, ...) {
    if (global_logger == NULL || global_logger->min_level > LOG_INFO) return;
    
    va_list args;
    va_start(args, format);
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    FILE *out = global_logger->output_file;
    if (global_logger->use_colors && out == stdout) fprintf(out, "%s", get_color_code(LOG_INFO));
    if (global_logger->use_timestamps) fprintf(out, "[%s] ", timestamp);
    fprintf(out, "[INFO] ");
    vfprintf(out, format, args);
    fprintf(out, "\n");
    if (global_logger->use_colors && out == stdout) fprintf(out, "\033[0m");
    fflush(out);
    va_end(args);
}

void logger_warn(const char *format, ...) {
    if (global_logger == NULL || global_logger->min_level > LOG_WARN) return;
    
    va_list args;
    va_start(args, format);
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    FILE *out = global_logger->output_file;
    if (global_logger->use_colors && out == stdout) fprintf(out, "%s", get_color_code(LOG_WARN));
    if (global_logger->use_timestamps) fprintf(out, "[%s] ", timestamp);
    fprintf(out, "[WARN] ");
    vfprintf(out, format, args);
    fprintf(out, "\n");
    if (global_logger->use_colors && out == stdout) fprintf(out, "\033[0m");
    fflush(out);
    va_end(args);
}

void logger_error(const char *format, ...) {
    if (global_logger == NULL || global_logger->min_level > LOG_ERROR) return;
    
    va_list args;
    va_start(args, format);
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    FILE *out = global_logger->output_file;
    if (global_logger->use_colors && out == stdout) fprintf(out, "%s", get_color_code(LOG_ERROR));
    if (global_logger->use_timestamps) fprintf(out, "[%s] ", timestamp);
    fprintf(out, "[ERROR] ");
    vfprintf(out, format, args);
    fprintf(out, "\n");
    if (global_logger->use_colors && out == stdout) fprintf(out, "\033[0m");
    fflush(out);
    va_end(args);
}

void logger_critical(const char *format, ...) {
    if (global_logger == NULL) return;
    
    va_list args;
    va_start(args, format);
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    FILE *out = global_logger->output_file;
    if (global_logger->use_colors && out == stdout) fprintf(out, "%s", get_color_code(LOG_CRITICAL));
    if (global_logger->use_timestamps) fprintf(out, "[%s] ", timestamp);
    fprintf(out, "[CRITICAL] ");
    vfprintf(out, format, args);
    fprintf(out, "\n");
    if (global_logger->use_colors && out == stdout) fprintf(out, "\033[0m");
    fflush(out);
    va_end(args);
}

void logger_hexdump(const char *label, const uint8_t *data, size_t length) {
    if (global_logger == NULL || global_logger->min_level > LOG_DEBUG) return;
    if (data == NULL || length == 0) return;

    FILE *out = global_logger->output_file;
    if (global_logger->use_colors && out == stdout) fprintf(out, "%s", get_color_code(LOG_DEBUG));
    if (global_logger->use_timestamps) {
        time_t now = time(NULL);
        struct tm *timeinfo = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
        fprintf(out, "[%s] ", timestamp);
    }
    fprintf(out, "[HEXDUMP] %s:\n", label);

    for (size_t i = 0; i < length; i += 16) {
        fprintf(out, "  %04zx: ", i);
        
        for (size_t j = 0; j < 16 && i + j < length; j++) {
            fprintf(out, "%02x ", data[i + j]);
        }
        
        fprintf(out, " | ");
        
        for (size_t j = 0; j < 16 && i + j < length; j++) {
            unsigned char c = data[i + j];
            fprintf(out, "%c", (c >= 32 && c < 127) ? c : '.');
        }
        
        fprintf(out, "\n");
    }

    if (global_logger->use_colors && out == stdout) fprintf(out, "\033[0m");
    fflush(out);
}
