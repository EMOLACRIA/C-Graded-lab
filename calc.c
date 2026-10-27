// Emirhan Kucukyilmaz 251ADB105
// https://github.com/EMOLACRIA/C-Graded-lab
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude (Anthropic) - <bu konuşmanın paylaşım linki>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define STUDENT_NAME "emirhan"
#define STUDENT_LASTNAME "kucukyilmaz"
#define STUDENT_ID "251ADB105"

//################################################################################//
// AI (Claude): prompt "Write the comments for my code" Link: <conversation link> //
// I wrote the code myself; Claude wrote the explanatory comments.                //
//################################################################################//

// A result is either an integer or a float, never both. is_float says which
// field is valid: 0 means use "integer", 1 means use "real". If either
// operand of an operation is a float, the operation is done in double and
// the result is a float. Only int op int stays integer arithmetic, so
// 7 / 2 gives 3 but 7 / 2.0 gives 3.5.
typedef struct {
    long long integer;
    double real;
    int is_float;
} Value;


// Positions start at 1, so 0 can mean "no error yet". This is a global
// because the parser is recursive and an error can happen at any depth;
// passing the position through every function would clutter every signature.
static size_t first_error_pos = 0;


// Only the first error is reported. While the parser returns from a deeper
// failure, the functions above it may detect a second problem. Without this
// check they would overwrite the real, earlier position.
static void set_error(size_t position)
{
    if (first_error_pos == 0) {
        first_error_pos = position;
    }
}

// Results are collected in memory instead of being written to the file
// directly, because on any error the output file must contain only
// ERROR:<pos> and no partial results. length is the number of bytes in use,
// capacity is the number of bytes allocated, so length <= capacity always.
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} OutputBuffer;

// The capacity doubles instead of growing by n each time. Then every byte is
// copied only a few times in total, so output of several megabytes stays
// linear in time instead of being copied again on every append.
// The realloc result goes into new_data first: if realloc fails it returns
// NULL, and assigning that straight to buffer->data would lose the old block.
// The two overflow checks stop size_t arithmetic from wrapping around, which
// would give realloc a size too small for the memcpy below.
static void output_append(OutputBuffer *buffer, const char *bytes, size_t n)
{
    size_t needed;

    if (n == 0) return;

    if (n > (size_t)-1 - buffer->length) {
        fprintf(stderr, "Output is too large\n");
        exit(1);
    }

    needed = buffer->length + n;

    if (needed > buffer->capacity) {
        // Start at 256 because doubling a capacity of 0 would stay 0 and
        // the loop below would never end.
        size_t new_capacity = buffer->capacity ? buffer->capacity : 256;
        char *new_data;

        while (new_capacity < needed) {
            if (new_capacity > (size_t)-1 / 2) {
                new_capacity = needed;
                break;
            }
            new_capacity *= 2;
        }

        new_data = realloc(buffer->data, new_capacity);
        if (new_data == NULL) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }

        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    memcpy(buffer->data + buffer->length, bytes, n);
    buffer->length += n;
}