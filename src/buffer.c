#include <stdlib.h>
#include <string.h>
#include "buffer.h"
#include "logger.h"

circular_buffer_t* buffer_create(size_t capacity) {
    if (capacity == 0) {
        logger_error("Invalid buffer capacity");
        return NULL;
    }

    circular_buffer_t *buffer = (circular_buffer_t *)malloc(sizeof(circular_buffer_t));
    if (buffer == NULL) {
        logger_error("Failed to allocate memory for buffer structure");
        return NULL;
    }

    buffer->data = (uint8_t *)malloc(capacity);
    if (buffer->data == NULL) {
        logger_error("Failed to allocate memory for buffer data");
        free(buffer);
        return NULL;
    }

    buffer->capacity = capacity;
    buffer->used = 0;
    buffer->head = 0;
    buffer->tail = 0;

    logger_debug("Circular buffer created (capacity: %zu bytes)", capacity);
    return buffer;
}

void buffer_free(circular_buffer_t *buffer) {
    if (buffer == NULL) return;

    if (buffer->data != NULL) {
        free(buffer->data);
    }
    free(buffer);

    logger_debug("Circular buffer freed");
}

int buffer_write(circular_buffer_t *buffer, const uint8_t *data, size_t length) {
    if (buffer == NULL || data == NULL || length == 0) {
        logger_error("Invalid buffer or data for write operation");
        return -1;
    }

    if (buffer->used + length > buffer->capacity) {
        logger_warn("Insufficient buffer space (used: %zu, need: %zu, capacity: %zu)",
                    buffer->used, length, buffer->capacity);
        return -1;
    }

    /* Write data */
    size_t write_pos = buffer->tail;
    for (size_t i = 0; i < length; i++) {
        buffer->data[write_pos] = data[i];
        write_pos = (write_pos + 1) % buffer->capacity;
    }

    buffer->tail = write_pos;
    buffer->used += length;

    logger_debug("Wrote %zu bytes to buffer (used: %zu/%zu)", length, buffer->used, buffer->capacity);
    return 0;
}

int buffer_read(circular_buffer_t *buffer, uint8_t *data, size_t length) {
    if (buffer == NULL || data == NULL || length == 0) {
        logger_error("Invalid buffer or data for read operation");
        return -1;
    }

    if (buffer->used < length) {
        logger_warn("Insufficient data in buffer (available: %zu, requested: %zu)",
                    buffer->used, length);
        return -1;
    }

    /* Read data */
    size_t read_pos = buffer->head;
    for (size_t i = 0; i < length; i++) {
        data[i] = buffer->data[read_pos];
        read_pos = (read_pos + 1) % buffer->capacity;
    }

    buffer->head = read_pos;
    buffer->used -= length;

    logger_debug("Read %zu bytes from buffer (remaining: %zu/%zu)", length, buffer->used, buffer->capacity);
    return 0;
}

size_t buffer_get_available(circular_buffer_t *buffer) {
    if (buffer == NULL) return 0;
    return buffer->used;
}

void buffer_reset(circular_buffer_t *buffer) {
    if (buffer == NULL) return;

    buffer->head = 0;
    buffer->tail = 0;
    buffer->used = 0;

    logger_debug("Buffer reset");
}
