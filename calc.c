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

typedef struct {
    long long integer;
    double real;
    int is_float;
} Value;