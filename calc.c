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


// power calls itself for the exponent, so 2 ** 3 ** 2 groups as
// 2 ** (3 ** 2) = 512. That is why ** is right-associative here: the
// right side is parsed first, as a whole power. A loop like the one in
// term would group it as (2 ** 3) ** 2 = 64, which is wrong.
// Two positions are saved before the tokens are consumed. op_pos is the
// '**' itself, used when a float result is infinite or not a number, as
// the task reports that at the operator. exp_pos is the first token of
// the exponent, used for a negative integer exponent, which the task
// reports at the start of the exponent. Both must be read before advance
// moves current on.
// After each recursive call first_error_pos is checked, so a reported
// error stops the calculation instead of feeding an unused zero Value to
// value_pow.
static Value parse_power(Parser *p)
{
    Value left = parse_primary(p);

    if (first_error_pos != 0) {
        return left;
    }

    if (p->current.type != TOK_POWER) {
        return left;
    }

    size_t op_pos = p->current.position;
    advance(p);
    size_t exp_pos = p->current.position;

    Value right = parse_power(p);

    if (first_error_pos != 0) {
        return right;
    }

    Value result = value_pow(left, right, exp_pos);
    check_finite(result, op_pos);
    return result;
}


// term uses a loop and not recursion, so * and / group from the left:
// 8 / 4 / 2 is (8 / 4) / 2 = 1. Recursing on the right side, as power
// does, would give 8 / (4 / 2) = 4, which is wrong. The calls to
// parse_power are what give * and / a lower precedence than **, because
// every operand of * and / is a whole power.
// Two positions are saved before the tokens are consumed: op_pos is the
// operator, used when a float result is infinite or not a number, and
// right_pos is the first token of the right operand, which is where the
// task reports a division by zero. They differ, so "4 / 0" reports the
// '0', not the '/'.
// first_error_pos is checked after every call that can fail, including
// after the division itself, so the loop stops at the first error instead
// of going on with the zero Value that value_div returns.
static Value parse_term(Parser *p)
{
    Value left = parse_power(p);

    if (first_error_pos != 0) {
        return left;
    }

    while (p->current.type == TOK_STAR ||
           p->current.type == TOK_SLASH) {
        TokenType op = p->current.type;
        size_t op_pos = p->current.position;

        advance(p);
        size_t right_pos = p->current.position;

        Value right = parse_power(p);

        if (first_error_pos != 0) {
            return right;
        }

        if (op == TOK_STAR) {
            left = value_mul(left, right);
        } else {
            left = value_div(left, right, right_pos);
        }

        check_finite(left, op_pos);

        if (first_error_pos != 0) {
            return left;
        }
           }

    return left;
}

// expr is the lowest precedence level, so + and - get their operands from
// parse_term. Every operand is a whole term, which means 3 + 2 * 5 reads
// the right side of the '+' as 2 * 5 and * binds tighter than +. The
// precedence comes from which function is called, there is no table.
// Like term, this is a loop, so + and - group from the left:
// 10 - 3 - 2 is (10 - 3) - 2 = 5, not 10 - (3 - 2) = 9.
// op_pos is saved before advance, because a float result that is infinite
// or not a number is reported at the operator. There is no right_pos here:
// + and - have no error that is reported at the right operand.
// first_error_pos is checked after each call that can fail, so a reported
// error stops the loop immediately.
static Value parse_expr(Parser *p)
{
    Value left = parse_term(p);

    if (first_error_pos != 0) {
        return left;
    }

    while (p->current.type == TOK_PLUS ||
           p->current.type == TOK_MINUS) {
        TokenType op = p->current.type;
        size_t op_pos = p->current.position;

        advance(p);
        Value right = parse_term(p);

        if (first_error_pos != 0) {
            return right;
        }

        if (op == TOK_PLUS) {
            left = value_add(left, right);
        } else {
            left = value_sub(left, right);
        }

        check_finite(left, op_pos);

        if (first_error_pos != 0) {
            return left;
        }
           }

    return left;
}


// The loop walks the file once, line by line, and index i is always a
// position in the whole file, so the tokenizer positions already count
// every character, including blank lines and comment lines. Nothing has to
// be recounted per line.
// first_error_pos is reset first because it is a global: with -d several
// files are evaluated in one run, and an error of one file must not leak
// into the next.
// Blank lines and comment lines produce no output. Only a '#' that is the
// first non-blank character starts a comment; a '#' later in a line is
// not seen here, so the tokenizer reports it as an invalid character, as
// the task requires. The end of the file is decided by length and not by
// '\0', because a '\0' byte in the file is an invalid character, not an end.
// After parse_expr the line must be finished. If the current token is not
// TOK_EOL, something is left that the grammar cannot use, such as the
// second ')' in "(1 + 2))", so the error is at that token.
// On any error the partial results are thrown away with output_reset and
// only ERROR:<pos> is written, because the task allows no partial output.
// Since parse_expr stops at the first error, the first line with an error
// is also the first error in the file.
// TOK_EOL does not move the index, so after a line i points at its '\n'
// or at the end of the file; the '\n' is skipped here so the next turn
// starts on a new line.
static void evaluate_text(const OutputBuffer *input, OutputBuffer *output)
{
    const char *text = input->data;
    size_t i = 0;

    first_error_pos = 0;

    while (i < input->length) {
        size_t j = i;

        while (j < input->length &&
               (text[j] == ' ' || text[j] == '\t')) {
            j++;
               }

        if (j >= input->length || text[j] == '\n') {
            if (j < input->length) {
                j++;
            }
            i = j;
            continue;
        }

        if (text[j] == '#') {
            while (j < input->length && text[j] != '\n') {
                j++;
            }
            if (j < input->length) {
                j++;
            }
            i = j;
            continue;
        }

        Parser p = {0};
        p.tz.text = text;
        p.tz.length = input->length;
        p.tz.index = i;

        advance(&p);
        Value v = parse_expr(&p);

        if (first_error_pos == 0 && p.current.type != TOK_EOL) {
            set_error(p.current.position);
        }

        if (first_error_pos != 0) {
            output_reset(output);
            output_error(output, first_error_pos);
            return;
        }

        output_value(output, v);

        i = p.tz.index;
        if (i < input->length && text[i] == '\n') {
            i++;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input-file>\n", argv[0]);
        return 1;
    }

    OutputBuffer in = {0};
    OutputBuffer out = {0};

    if (read_file(argv[1], &in) == -1) {
        fprintf(stderr, "Cannot read '%s': %s\n",
                argv[1], strerror(errno));
        return 1;
    }

    evaluate_text(&in, &out);

    if (out.length != 0) {
        fwrite(out.data, 1, out.length, stdout);
    }

    output_free(&in);
    output_free(&out);
    return 0;
}

// Takes the last part of a path as the base name: "../in/a.txt" gives "a"
// and "labs/" gives "labs". Trailing '/' are dropped first, otherwise the
// last part of "labs/" would look empty. strip_txt is 1 for an input file
// and 0 for a folder given with -d, because a folder named "data.txt" must
// keep its name. The suffix is copied into a 5-byte array and compared
// there, so the comparison never runs past the end of the part. The length
// must be above 4, so a file that is exactly ".txt" is not turned into an
// empty name. An empty result is an error, and so is a name that does not
// fit in out, so out is never written past its size.
static int base_name(const char *path, int strip_txt, char *out, size_t size)
{
    size_t end = strlen(path);
    size_t start;
    size_t length;

    while (end > 0 && path[end - 1] == '/') {
        end--;
    }

    start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }

    length = end - start;

    if (strip_txt && length > 4) {
        char suffix[5];

        memcpy(suffix, path + end - 4, 4);
        suffix[4] = '\0';

        if (strcmp(suffix, ".txt") == 0) {
            length -= 4;
        }
    }

    if (length == 0) {
        return -1;
    }

    if (length >= size) {
        return -1;
    }

    memcpy(out, path + start, length);
    out[length] = '\0';
    return 0;
}

// The result file name is <base>_<name>_<lastname>_<id>.txt, exactly the
// form the task gives. snprintf is checked for a negative value before the
// cast to size_t, and for truncation, so a long name is an error and never
// a cut-off file name.
static int result_file_name(char *out, size_t size, const char *base)
{
    int count = snprintf(out, size, "%s_%s_%s_%s.txt",
                         base, STUDENT_NAME, STUDENT_LASTNAME, STUDENT_ID);

    if (count < 0 || (size_t)count >= size) {
        return -1;
    }
    return 0;
}

// The default output folder has no last name: <base>_<name>_<id>, like
// "labs_anna_221234" in the task. It is created in the current folder, not
// next to the input.
static int default_dir_name(char *out, size_t size, const char *base)
{
    int count = snprintf(out, size, "%s_%s_%s",
                         base, STUDENT_NAME, STUDENT_ID);

    if (count < 0 || (size_t)count >= size) {
        return -1;
    }
    return 0;
}

// Creates the output folder, and accepts one that already exists, as the
// task allows writing into an existing folder. Only the last level is
// created, because the task says the parent folder exists.
// If mkdir fails for any reason other than "already exists", errno is left
// as it is, so the caller can print strerror(errno) for the real cause.
// EEXIST alone is not enough: a regular file with the same name also gives
// EEXIST, so stat checks that it is really a folder. In that case errno is
// set to ENOTDIR so the message says what is wrong.
static int make_output_dir(const char *dir)
{
    if (mkdir(dir, 0777) == 0) {
        return 0;
    }

    if (errno != EEXIST) {
        return -1;
    }

    struct stat info;
    if (stat(dir, &info) != 0) {
        return -1;
    }

    if (!S_ISDIR(info.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    return 0;
}


// Evaluates one input file and writes its result file into out_dir.
// An ERROR:<pos> result is a normal result, so it returns 0; only a real
// failure (unreadable input, folder or file that cannot be written)
// returns -1, and main turns that into a non-zero exit code.
// The file is read and evaluated before the output folder is created, so a
// missing input file does not leave an empty folder behind.
// Every failure jumps to one cleanup label instead of freeing in each
// branch, so both buffers are freed on every path and the sanitizer build
// reports no leak. All variables are declared before the first goto, so no
// jump passes a declaration. in and out start zeroed, which makes
// output_free safe even if read_file already freed in.
// errno is saved right after the failing call because fprintf may change
// it. When a call leaves it at 0, EIO is used so the message never says
// "Success" for an error.
// fclose is checked as well: a write error such as a full disk may only
// appear there. When out.length is 0 (the file had only comments or blank
// lines) fwrite is skipped, because out.data may be NULL, and an empty
// result file is written.
static int process_file(const char *input_path, const char *out_dir)
{
    OutputBuffer in = {0};
    OutputBuffer out = {0};
    char base[4096];
    char name[4096];
    char full[4096];
    FILE *f;
    int count;
    int write_error = 0;
    int status = -1;

    if (base_name(input_path, 1, base, sizeof base) == -1 ||
        result_file_name(name, sizeof name, base) == -1) {
        fprintf(stderr, "Invalid file name: '%s'\n", input_path);
        goto cleanup;
    }

    count = snprintf(full, sizeof full, "%s/%s", out_dir, name);
    if (count < 0 || (size_t)count >= sizeof full) {
        fprintf(stderr, "Output path is too long: '%s/%s'\n",
                out_dir, name);
        goto cleanup;
    }

    errno = 0;
    if (read_file(input_path, &in) == -1) {
        int saved_errno = errno;
        fprintf(stderr, "Cannot read '%s': %s\n", input_path,
                strerror(saved_errno != 0 ? saved_errno : EIO));
        goto cleanup;
    }

    evaluate_text(&in, &out);

    if (make_output_dir(out_dir) == -1) {
        int saved_errno = errno;
        fprintf(stderr, "Cannot create directory '%s': %s\n",
                out_dir, strerror(saved_errno));
        goto cleanup;
    }

    errno = 0;
    f = fopen(full, "wb");
    if (f == NULL) {
        int saved_errno = errno;
        fprintf(stderr, "Cannot open '%s': %s\n", full,
                strerror(saved_errno != 0 ? saved_errno : EIO));
        goto cleanup;
    }

    if (out.length > 0) {
        errno = 0;
        if (fwrite(out.data, 1, out.length, f) != out.length) {
            int saved_errno = errno;
            fprintf(stderr, "Cannot write '%s': %s\n", full,
                    strerror(saved_errno != 0 ? saved_errno : EIO));
            write_error = 1;
        }
    }

    errno = 0;
    if (fclose(f) != 0) {
        int saved_errno = errno;
        fprintf(stderr, "Cannot close '%s': %s\n", full,
                strerror(saved_errno != 0 ? saved_errno : EIO));
        write_error = 1;
    }

    if (!write_error) {
        status = 0;
    }

cleanup:
    output_free(&in);
    output_free(&out);
    return status;
}


// Evaluates every *.txt file directly inside dir. Files without the .txt
// ending and sub folders are skipped: a name is checked by its last four
// characters, and stat with S_ISREG makes sure it is a regular file, so a
// folder named like "x.txt" is ignored too. DT_REG is not used because it
// is not defined under -std=c17.
// A failure in one file does not stop the others. status becomes -1 and the
// loop goes on, so one bad file does not keep the rest from being written;
// every failing file has already printed its own message.
// readdir returns NULL both at the end of the folder and on an error, so
// errno is set to 0 before each call. After the loop a non-zero errno
// means a real read error.
static int process_dir(const char *dir, const char *out_dir)
{
    DIR *d = opendir(dir);
    struct dirent *entry;
    struct stat info;
    char path[4096];
    int status = 0;

    if (d == NULL) {
        int saved_errno = errno;
        fprintf(stderr, "Cannot open directory '%s': %s\n",
                dir, strerror(saved_errno));
        return -1;
    }

    while ((errno = 0, (entry = readdir(d)) != NULL)) {
        const char *name = entry->d_name;
        size_t len = strlen(name);

        if (len < 4 || strcmp(name + len - 4, ".txt") != 0) {
            continue;
        }

        int count = snprintf(path, sizeof path, "%s/%s", dir, name);
        if (count < 0 || (size_t)count >= sizeof path) {
            fprintf(stderr, "Input path is too long: '%s/%s'\n", dir, name);
            status = -1;
            continue;
        }

        if (stat(path, &info) != 0) {
            int saved_errno = errno;
            fprintf(stderr, "Cannot inspect '%s': %s\n",
                    path, strerror(saved_errno));
            status = -1;
            continue;
        }

        if (!S_ISREG(info.st_mode)) {
            continue;
        }

        if (process_file(path, out_dir) == -1) {
            status = -1;
        }
    }

    if (errno != 0) {
        int saved_errno = errno;
        fprintf(stderr, "Cannot read directory '%s': %s\n",
                dir, strerror(saved_errno));
        status = -1;
    }

    if (closedir(d) != 0) {
        int saved_errno = errno;
        fprintf(stderr, "Cannot close directory '%s': %s\n",
                dir, strerror(saved_errno));
        status = -1;
    }

    return status;
}