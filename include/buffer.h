#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdint.h>

/* Circular Buffer Structure */
typedef struct {
    uint8_t *data;              /* Buffer data */
    size_t capacity;            /* Total capacity */
    size_t used;                /* Currently used bytes */
    size_t head;                /* Head pointer */
    size_t tail;                /* Tail pointer */
} circular_buffer_t;

/* Function Declarations */
circular_buffer_t* buffer_create(size_t capacity);
void buffer_free(circular_buffer_t *buffer);
int buffer_write(circular_buffer_t *buffer, const uint8_t *data, size_t length);
int buffer_read(circular_buffer_t *buffer, uint8_t *data, size_t length);
size_t buffer_get_available(circular_buffer_t *buffer);
void buffer_reset(circular_buffer_t *buffer);

#endif /* BUFFER_H */
