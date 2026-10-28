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


// Formats use exactly the task's rules: %lld for integers and %.15g for
// floats, so 0.1 + 0.2 prints 0.3. The line is built in a small local array
// because one result is always short, which avoids a heap allocation per
// line in files with millions of lines. The size check exists so a failed
// or truncated snprintf can never put a cut-off number into the output.
static void output_value(OutputBuffer *buffer, Value value)
{
    char line[64];
    int count;

    if (value.is_float) {
        count = snprintf(line, sizeof line, "%.15g\n", value.real);
    }
    else {
        count = snprintf(line, sizeof line, "%lld\n", value.integer);
    }

    if (count < 0 || (size_t)count >= sizeof line) {
        fprintf(stderr, "Cannot format result\n");
        exit(1);
    }

    output_append(buffer, line, (size_t)count);
}


// The error line goes through the same buffer as normal results, so the
// caller can reset the buffer first and then write only this one line.
// The count test is checked before the cast to size_t, because a negative
// int would turn into a huge unsigned number.
static void output_error(OutputBuffer *buffer, size_t position)
{
    char line[64];
    int count = snprintf(line, sizeof line, "ERROR:%zu\n", position);

    if (count < 0 || (size_t)count >= sizeof line) {
        fprintf(stderr, "Cannot format result\n");
        exit(1);
    }

    output_append(buffer, line, (size_t)count);
}


// Only length is cleared. The allocated block stays, so when an error
// throws away the partial results, or the next file starts, the memory is
// reused instead of being allocated again.
static void output_reset(OutputBuffer *buffer)
{
    buffer->length = 0;
}


// Pointers are set to NULL and sizes to 0 after free so a second call, or
// a later append, cannot touch freed memory. Every exit path must call
// this, otherwise the sanitizer build reports a leak.
static void output_free(OutputBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}


// The whole file is read into memory at once because error positions count
// every character from the start of the file, comments included. With the
// file in one buffer, a position is just an index plus 1 and no separate
// counter has to be carried across lines.
// The file is read in 64 KB pieces into output_append, so the buffer grows
// by doubling and large files are not copied again on every read.
// "rb" is used so the bytes are not changed on the way in, which would
// shift the positions.
// fread returning 0 means either end of file or a read error, so ferror is
// checked afterwards: a path that is a folder opens fine but fails here.
// On failure the buffer is freed here, so the caller has nothing to clean up.
// A '\0' is stored after the data but length is decreased again, so
// functions such as strtod cannot read past the end, while a real '\0' byte
// in the file still counts as data and is reported as an invalid character.
// Returns 0 on success and -1 if the file cannot be opened or read.
static int read_file(const char *path, OutputBuffer *buffer)
{
    FILE *file = fopen(path, "rb");
    char chunk[65536];
    size_t n;

    if (file == NULL) {
        return -1;
    }

    while ((n = fread(chunk, 1, sizeof chunk, file)) > 0){
        output_append(buffer, chunk, n);
    }

    if (ferror(file)) {
        fclose(file);
        output_free(buffer);
        return -1;
    }

    output_append(buffer, "", 1);

    buffer->length--;
    fclose(file);
    return 0;
}


// One token kind per thing the parser must tell apart. ** is its own kind
// instead of two TOK_STAR, so the parser can give it a higher precedence
// than * without looking at the next token. TOK_EOL stands for both a
// newline and the end of the file, because in both cases the line has
// ended and the error rules treat them the same way.
typedef enum {
    TOK_NUMBER,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_POWER,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_EOL,
    TOK_INVALID
} TokenType;


// position is stored in the token itself, because every error is reported
// at the start of a token. The parser can then report the position of the
// token it was looking at without asking the tokenizer again. value is only
// meaningful when type is TOK_NUMBER.
typedef struct {
    TokenType type;
    size_t position;
    Value value;
} Token;


// The tokenizer works on the whole file in memory. index is the 0-based
// offset of the next unread character, so the 1-based position of a
// character is index + 1. No separate position counter is needed, and
// newlines and comment lines are counted automatically because index
// moves over every character of the file. The end is decided by length and
// not by '\0', because a real '\0' byte inside the file is data.
typedef struct {
    const char *text;
    size_t length;
    size_t index;
} Tokenizer;


// Only spaces and tabs are skipped, because the task allows only these
// between tokens. A '\n' must not be skipped here: it ends the line, and
// the line loop needs to see it as TOK_EOL.
// The position is taken as index + 1 before index moves, so it always
// points at the first character of the token. This is how errors get the
// 1-based position without a separate counter.
// For TOK_EOL, index is not advanced. Then the file end gives
// position length + 1 and a '\n' gives its own position, which are exactly
// the positions the task wants for "missing operand" and "missing )".
// * looks one character ahead for a second * so that ** becomes one token;
// the length check comes first so the lookahead never reads past the text.
// Any other character, including '#' in the middle of a line, becomes
// TOK_INVALID and index still moves by one, otherwise the next call would
// return the same invalid character forever.
static Token next_token(Tokenizer *t)
{
    while (t->index < t->length && (t->text[t->index] == ' ' || t->text[t->index] == '\t')) {
        t->index++;
    }
    Token token = {0};
    token.position = t->index + 1;

    if (t->index >= t->length) {
        token.type = TOK_EOL;
        return token;
    }

    if (t->text[t->index] == '\n') {
        token.type = TOK_EOL;
        return token;
    }



    // A number is scanned by hand first, and only then converted. The scan
    // decides where the token ends, and index is left exactly there, so
    // strtod/strtoll never decide the token length. This matters because
    // strtod also accepts forms the task forbids, such as 1.5e3, .5 and 5.
    // Here a '.' is taken only if a digit follows it. So "5." is the
    // integer 5 followed by an invalid '.', "1.5e3" is 1.5 followed by an
    // invalid 'e', and ".5" never enters this branch because '.' is not a
    // digit. The three forbidden forms need no extra code and the error
    // lands on the '.' or the 'e' as the task requires.
    // The int/float decision is made by the '.': a number with a decimal
    // part is a float, any other number is an integer.
    // Both checks put "index < length" first so the text is never read past
    // its end. (unsigned char) is used because isdigit with a negative char
    // value is undefined behaviour.
    if (isdigit((unsigned char)t->text[t->index])) {
        size_t start = t->index;
        int is_float = 0;

        while (t->index < t->length &&
               isdigit((unsigned char)t->text[t->index])) {
            t->index++;
        }

        if (t->index + 1 < t->length &&
            t->text[t->index] == '.' &&
            isdigit((unsigned char)t->text[t->index + 1])) {
            is_float = 1;
            t->index++;

            while (t->index < t->length &&
                   isdigit((unsigned char)t->text[t->index])) {
                t->index++;
            }
        }

        // The '\0' stored after the file data (see read_file) stops these
        // two functions at the end of the text. Base 10 is given on purpose
        // so a leading 0 does not turn the number into octal.
        if (is_float) {
            token.value.real = strtod(t->text + start, NULL);
        } else {
            token.value.integer = strtoll(t->text + start, NULL, 10);
        }

        token.value.is_float = is_float;
        token.type = TOK_NUMBER;
        return token;
    }

    switch (t->text[t->index]) {
        case '+':
            token.type = TOK_PLUS;
            t->index++;
            break;

        case '-':
            token.type = TOK_MINUS;
            t->index++;
            break;

        case '/':
            token.type = TOK_SLASH;
            t->index++;
            break;

        case '(':
            token.type = TOK_LPAREN;
            t->index++;
            break;

        case ')':
            token.type = TOK_RPAREN;
            t->index++;
            break;

        case '*':
            if (t->index + 1 < t->length && t->text[t->index + 1] == '*') {
                token.type = TOK_POWER;
                t->index += 2;
            } else {
                token.type = TOK_STAR;
                t->index++;
            }
            break;

        default:
            token.type = TOK_INVALID;
            t->index++;
            break;
    }

    return token;
}


// Mixed arithmetic is done in double: if only one side is a float, the
// integer side is converted here, so the three operation functions below do
// not each need their own conversion. Integers above 2^53 can lose
// precision in this conversion; the float tests use small numbers.
static double value_as_double(Value value)
{
    return value.is_float ? value.real : (double)value.integer;
}

// This is where the int/float decision is made. One float operand is
// enough to make the result a float, so the test is "a || b". Only
// int op int stays in long long arithmetic, which is why 7 / 2 can later
// truncate while 7 / 2.0 does not. Overflow is not checked, because the
// task says the tests never overflow.
static Value value_add(Value a, Value b)
{
    Value result = {0};

    if (a.is_float || b.is_float) {
        result.is_float = 1;
        result.real = value_as_double(a) + value_as_double(b);
    } else {
        result.integer = a.integer + b.integer;
    }

    return result;
}

// Same rule as value_add: the result is a float if either operand is.
static Value value_sub(Value a, Value b)
{
    Value result = {0};

    if (a.is_float || b.is_float) {
        result.is_float = 1;
        result.real = value_as_double(a) - value_as_double(b);
    } else {
        result.integer = a.integer - b.integer;
    }

    return result;
}

// Same rule as value_add: the result is a float if either operand is.
static Value value_mul(Value a, Value b)
{
    Value result = {0};

    if (a.is_float || b.is_float) {
        result.is_float = 1;
        result.real = value_as_double(a) * value_as_double(b);
    } else {
        result.integer = a.integer * b.integer;
    }

    return result;
}

// The zero check looks only at the divisor and uses its own type, so
// 1.5 / 0 and 10 / 0.0 are errors just like 10 / 0, as the task requires.
// The error goes to right_pos, the start of the right operand, not to the
// '/' operator, because the task reports division by zero there.
// The check comes before the division: an integer division by zero is
// undefined behaviour, and a float one would give inf instead of an error.
// A zero Value is returned after the error so the caller has something to
// carry on with; it then sees first_error_pos is set and stops.
static Value value_div(Value a, Value b, size_t right_pos)
{
    Value result = {0};

    if (b.is_float ? b.real == 0.0 : b.integer == 0) {
        set_error(right_pos);
        return result;
    }

    if (a.is_float || b.is_float) {
        result.is_float = 1;
        result.real = value_as_double(a) / value_as_double(b);
    } else {
        // C truncates integer division toward zero, which is what the task
        // wants: 7 / 2 is 3 and (0 - 7) / 2 is -3. No extra code is needed.
        result.integer = a.integer / b.integer;
    }

    return result;
}

// Two paths. If either side is a float, pow() is used, so 0.5 ** (0 - 1)
// is 2 and a negative exponent is fine. Only int ** int is integer
// arithmetic, and there a negative exponent is an error at exp_pos, the
// start of the exponent, as the task requires.
// The integer power uses repeated squaring: each loop step handles one bit
// of the exponent, so 1 ** 1000000000 takes about 30 steps instead of a
// billion multiplications. The result starts at 1, so x ** 0 is 1,
// including 0 ** 0, without a special case.
// The base is squared only while the exponent is still above 0. A final
// squaring would be wasted and could overflow a long long, for example
// 3 ** 39 fits but squaring 3 ** 32 again does not, and signed overflow is
// undefined behaviour.
static Value value_pow(Value a, Value b, size_t exp_pos)
{
    Value result = {0};

    if (a.is_float || b.is_float) {
        result.is_float = 1;
        result.real = pow(value_as_double(a), value_as_double(b));
        return result;
    }

    if (b.integer < 0) {
        set_error(exp_pos);
        return result;
    }

    long long base = a.integer;
    long long exponent = b.integer;
    result.integer = 1;

    while (exponent > 0) {
        if (exponent % 2 != 0) {
            result.integer *= base;
        }

        exponent /= 2;

        if (exponent > 0) {
            base *= base;
        }
    }

    return result;
}

// Overflow to infinity and invalid results such as (0 - 8) ** 0.5 only
// show up in the double result, and the task reports them at the operator,
// which only the parser knows. So the arithmetic functions stay free of
// positions and the parser calls this after each operation instead.
// isfinite catches both infinity and NaN in one test. Integers are never
// checked: the task says the tests do not overflow them.
static void check_finite(Value v, size_t op_pos)
{
    if (v.is_float && !isfinite(v.real)) {
        set_error(op_pos);
    }
}


// The parser holds the tokenizer and the one token it is looking at. Every
// error is reported at current.position, the start of the token the parser
// could not use, which is the position rule of the task. Keeping that one
// token here means each function can report an error without asking the
// tokenizer again.
typedef struct {
    Tokenizer tz;
    Token current;
} Parser;

static void advance(Parser *p)
{
    p->current = next_token(&p->tz);
}

// parse_primary calls parse_expr for the inside of parentheses, and
// parse_expr reaches parse_primary again through the lower levels, so the
// function has to be declared before it is defined.
static Value parse_expr(Parser *p);

// primary is the innermost level of the grammar: a number or a
// parenthesised expression. Any other token here means an operand is
// missing, so the error is at that token: a second operator in "7 + * 3",
// the '\n' in "3 *", the ')' in "2 * ()" and an invalid character such as
// '&'. For "(2 + 3" the closing ')' is missing, so the error is at the
// token that appeared instead, the '\n'.
// There is no longjmp, so after parse_expr returns, first_error_pos is
// checked. Otherwise the code would go on looking for ')' after an error
// that was already reported. After an error a zero Value is returned; the
// callers never use it, they see first_error_pos is set and stop.
static Value parse_primary(Parser *p) {
    if (p->current.type == TOK_NUMBER) {
        Value value = p->current.value;
        advance(p);
        return value;
    }

    if (p->current.type == TOK_LPAREN) {
        advance(p);
        Value value = parse_expr(p);

        if (first_error_pos != 0) {
            return value;
        }

        if (p->current.type != TOK_RPAREN) {
            set_error(p->current.position);
            return (Value){0};
        }

        advance(p);
        return value;
    }

    set_error(p->current.position);
    return (Value){0};
}