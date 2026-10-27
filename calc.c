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

// AI (Claude): prompt "can you write the comments for my code" Link: <conversation link>
// I wrote the code myself; Claude wrote the explanatory comments.

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