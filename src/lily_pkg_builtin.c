#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lily_parser.h"
#include "lily_symtab.h"
#include "lily_utf8.h"
#include "lily_move.h"
#include "lily_value_flags.h"
#include "lily_value_structs.h"
#include "lily_alloc.h"
#include "lily_api_embed.h"
#include "lily_api_value.h"

/* When destroying a value with a gc tag, set the tag to this to prevent destroy
   from reentering it. The values are useless, but cannot be 0 or this will be
   optimized as a NULL pointer. */
const lily_gc_entry lily_gc_stopper =
{
    1,
    1,
    {.integer = 1},
    NULL
};

/* This represents a return type of "self", which is special-cased in function
   returns. */
static const lily_class raw_self =
{
    NULL,
    ITEM_TYPE_CLASS,
    0,
    LILY_SELF_ID,
    0,
    (lily_type *)&raw_self,
    "self",
    0,
    NULL,
    NULL,
    NULL,
    0,
    0,
    {0},
    0,
    NULL,
    NULL,
};

const lily_class *lily_self_class = &raw_self;

/* Similar to the above, this is the read-only class+type of Unit. */
static const lily_class raw_unit =
{
    NULL,
    ITEM_TYPE_CLASS,
    0,
    LILY_UNIT_ID,
    0,
    (lily_type *)&raw_unit,
    "Unit",
    0,
    NULL,
    NULL,
    NULL,
    0,
    0,
    {0},
    0,
    NULL,
    NULL,
};

const lily_type *lily_unit_type = (lily_type *)&raw_unit;

/**
embedded builtin

The builtin package provides the classes, vars, and functions that form the
foundation of Lily.
*/

/**
var stdin: File

Provides a wrapper around the `stdin` present within C.
*/

/**
var stderr: File

Provides a wrapper around the `stderr` present within C.
*/

/**
var stdout: File

Provides a wrapper around the `stdout` present within C.
*/

/**
define print[A](value: A)

Write `value` to `stdout`, plus a newline (`"\n"`). This is equivalent to
`stdout.print(value)`.

# Errors:

* `IOError` if `stdout` is closed, or not open for reading.
*/

/**
define calltrace: List[String]

Returns a `List` with one `String` for each function that is currently entered.
*/

/**
define assert(condition: Boolean, message: *String="")

If `condition` is `false`, raise `AssertionError` using `message`.
*/

/**
native AssertionError < Exception

This is a subclass of `Exception` that is raised when an `assert` is passed
`false` as the first argument.
*/

/**
class Boolean

The `Boolean` class represents a value that is either `true` or `false`.
*/

/**
method Boolean.to_i(self: Boolean): Integer

Convert a `Boolean` to an `Integer`. `true` becomes `1`, `false` becomes `0`.
*/
void lily_builtin_Boolean_to_i(lily_state *s)
{
    lily_return_integer(s, lily_arg_boolean(s, 0));
}

/**
method Boolean.to_s(self: Boolean): String

Convert a `Boolean` to a `String`.
*/
void lily_builtin_Boolean_to_s(lily_state *s)
{
    int input = lily_arg_boolean(s, 0);
    char *to_copy;

    if (input == 0)
        to_copy = "false";
    else
        to_copy = "true";

    lily_return_string(s, lily_new_string(to_copy));
}

/**
class Byte

The `Byte` class represents a wrapper over a single `Byte` value. A `Byte` value
is always unsigned, giving it a range from 0 to 255. `Byte` literals are written
using 't' as the suffix on an `Integer` value.
*/

/**
method Byte.to_i(self: Byte): Integer

Convert a `Byte` to an `Integer`.
*/
void lily_builtin_Byte_to_i(lily_state *s)
{
    lily_return_integer(s, lily_arg_byte(s, 0));
}

/**
class ByteString

The `ByteString` class represents a bag of bytes. A `ByteString` may have '\0'
values embedded within it. It may also have data that is not valid as utf-8.
The `ByteString` class currently does not support any primitive operations.
*/

/**
method ByteString.each_byte(self: ByteString, fn: Function(Byte))

Call `fn` for each `Byte` within the given `ByteString`.
*/
void lily_builtin_ByteString_each_byte(lily_state *s)
{
    lily_bytestring_val *sv = lily_arg_bytestring(s, 0);
    const char *input = lily_bytestring_raw(sv);
    int len = lily_bytestring_length(sv);
    int i;

    lily_call_prepare(s, lily_arg_function(s, 1));

    for (i = 0;i < len;i++) {
        lily_push_byte(s, (uint8_t)input[i]);
        lily_call_exec_prepared(s, 1);
    }
}

/**
method ByteString.encode(self: ByteString, encode: *String="error"): Option[String]

Attempt to transform the given `ByteString` into a `String`. The action taken
depends on the value of `encode`.

If encode is `"error"`, then invalid utf-8 or embedded '\0' values within `self`
will result in `None`.
*/
void lily_builtin_ByteString_encode(lily_state *s)
{
    lily_bytestring_val *input_bytestring = lily_arg_bytestring(s, 0);
    const char *encode_method;

    if (lily_arg_count(s) == 2)
        encode_method = lily_arg_string_raw(s, 1);
    else
        encode_method = "error";

    char *byte_buffer = NULL;

    if (strcmp(encode_method, "error") == 0) {
        byte_buffer = lily_bytestring_raw(input_bytestring);
        int byte_buffer_size = lily_bytestring_length(input_bytestring);

        if (lily_is_valid_sized_utf8(byte_buffer, byte_buffer_size) == 0) {
            lily_return_none(s);
            return;
        }
    }
    else {
        lily_return_none(s);
        return;
    }

    lily_container_val *variant = lily_new_some();
    lily_nth_set(variant, 0,
            lily_box_string(s, lily_new_string(byte_buffer)));
    lily_return_variant(s, variant);
}

/**
method ByteString.size(self: ByteString): Integer

Return the number of `Byte` values within `self`.
*/
void lily_builtin_ByteString_size(lily_state *s)
{
    lily_return_integer(s, lily_arg_bytestring(s, 0)->size);
}

/* This table indicates how many more bytes need to be successfully read after
   that particular byte for proper utf-8. -1 = invalid.
   Table copied from lily_lexer.c */
static const char follower_table[256] =
{
     /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 8 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* 9 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* A */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* B */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* C */-1,-1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
/* F */ 4, 4, 4, 4, 4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static lily_string_val *make_sv(lily_state *s, int size)
{
    lily_string_val *new_sv = lily_malloc(sizeof(lily_string_val));
    char *new_string = lily_malloc(sizeof(char) * size);

    new_sv->string = new_string;
    new_sv->size = size - 1;
    new_sv->refcount = 0;

    return new_sv;
}

void do_str_slice(lily_state *s, int is_bytestring)
{
    lily_string_val *sv = lily_arg_string(s, 0);
    int start = 0;
    int stop = sv->size;

    switch (lily_arg_count(s)) {
        case 3: stop = lily_arg_integer(s, 2);
        case 2: start = lily_arg_integer(s, 1);
    }

    if (stop < 0)
        stop = sv->size + stop;
    if (start < 0)
        start = sv->size + start;

    if (stop > sv->size ||
        start > sv->size ||
        start > stop) {
        if (is_bytestring == 0)
            lily_return_string(s, lily_new_string(""));
        else
            lily_return_bytestring(s, lily_new_bytestring(""));

        return;
    }

    char *raw = lily_string_raw(sv);
    if (is_bytestring == 0) {
        if (follower_table[(unsigned char)raw[start]] == -1 ||
            follower_table[(unsigned char)raw[stop]] == -1) {
            lily_return_string(s, lily_new_string(""));
            return;
        }
    }

    int new_size = (stop - start) + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    char *new_str = new_sv->string;
    strncpy(new_str, raw + start, new_size - 1);
    new_str[new_size - 1] = '\0';

    if (is_bytestring == 0)
        lily_return_string(s, new_sv);
    else
        lily_return_bytestring(s, (lily_bytestring_val *)new_sv);
}

/**
method ByteString.slice(self: ByteString, start: *Integer=0, stop: *Integer=-1): ByteString

Create a new `ByteString` copying a section of `self` from `start` to `stop`.

If a negative index is given, it is treated as an offset from the end of `self`,
with `-1` being considered the last element.

On error, this generates an empty `ByteString`. Error conditions are:

* Either `start` or `stop` is out of range.
* The `start` is larger than the `stop` (reversed).
*/
void lily_builtin_ByteString_slice(lily_state *s)
{
    do_str_slice(s, 1);
}

static void return_exception(lily_state *s, uint16_t id)
{
    lily_container_val *result;
    lily_instance_super(s, &result, id, 2);

    lily_nth_set(result, 0, lily_arg_value(s, 0));
    lily_nth_set(result, 1, lily_box_list(s, lily_new_list(0)));
    lily_return_value(s, lily_take_value(s));
}

/**
native DivisionByZeroError < Exception

The `DivisionByZeroError` is a subclass of `Exception` that is raised when
trying to divide or modulo by zero.
*/

/**
constructor DivisionByZeroError(m: String): DivisionByZeroError
*/
void lily_builtin_DivisionByZeroError_new(lily_state *s)
{
    return_exception(s, LILY_DBZERROR_ID);
}

/**
class Double

The `Double` class exists as a wrapper over a C double.
*/

/**
method Double.to_i(self: Double): Integer

Convert a `Double` to an `Integer`. This is done internally through a cast from
a C double, to int64_t, the type of `Integer`.
*/
void lily_builtin_Double_to_i(lily_state *s)
{
    int64_t integer_val = (int64_t)lily_arg_double(s, 0);

    lily_return_integer(s, integer_val);
}

/**
class Dynamic

The `Dynamic` class allows defering type checking until runtime. Creation of
`Dynamic` is done through `Dynamic(<value>)`. Extraction of values is done
through a cast: `.@(type)`. The result of a cast is `Option[<type>]`, with
`Some` on success and `None` on failure. Finally, casts are not allowed to hold
polymorphic types, such as `List` or `Hash` or `Function`, because Lily's vm
only holds class information at runtime.
*/

/**
constructor Dynamic[A](self: A): Dynamic

Constructs a new `Dynamic` value. Call it using `Dynamic(<value>)`.
*/
extern void lily_builtin_Dynamic_new(lily_state *);

/**
native Exception
    var @message: String
    var @traceback: List[String]

The `Exception` class is the base class of all exceptions. It defines two
properties: A `message` as `String`, and a `traceback` as `List[String]`. The
`traceback` field is rewritten whenever an exception instance is raised.
*/

/**
constructor Exception(m: String): Exception
*/
void lily_builtin_Exception_new(lily_state *s)
{
    return_exception(s, LILY_EXCEPTION_ID);
}

/**
class File

The `File` class provides a wrapper over a C FILE * struct. A `File` is closed
automatically when a scope exits (though not immediately). However, it is also
possible to manually close a `File`.
*/

/**
method File.close(self: File)

Close `self` if it is open, or do nothing if already closed.

For standard streams, this marks the `File` as closed, but does not actually
close the stream. Embedders, therefore, do not need to worry about standard
streams being altered or closed by Lily.
*/
void lily_builtin_File_close(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);

    if (filev->inner_file != NULL) {
        if (filev->is_builtin == 0)
            fclose(filev->inner_file);
        filev->inner_file = NULL;
    }

    lily_return_unit(s);
}

/**
method File.each_line(self: File, fn: Function(ByteString))

Read each line of text from `self`, passing it down to `fn` for processing.

# Errors

* `IOError` if `self` is not open for reading, or is closed.
*/
void lily_builtin_File_each_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_msgbuf *vm_buffer = lily_get_clean_msgbuf(s);
    char read_buffer[128];
    int ch = 0, pos = 0;

    FILE *f = lily_file_for_read(s, filev);

    lily_call_prepare(s, lily_arg_function(s, 1));

    /* This uses fgetc in a loop because fgets may read in \0's, but doesn't
       tell how much was written. */
    while (1) {
        ch = fgetc(f);

        if (ch == EOF)
            break;

        if (pos == sizeof(read_buffer)) {
            lily_mb_add_slice(vm_buffer, read_buffer, 0, sizeof(read_buffer));
            pos = 0;
        }

        read_buffer[pos] = (char)ch;

        /* \r is intentionally not checked for, because it's been a very, very
           long time since any os used \r alone for newlines. */
        if (ch == '\n') {
            if (pos != 0) {
                lily_mb_add_slice(vm_buffer, read_buffer, 0, pos);
                pos = 0;
            }

            const char *text = lily_mb_get(vm_buffer);

            lily_push_bytestring(s, lily_new_bytestring(text));
            lily_call_exec_prepared(s, 1);
            lily_mb_flush(vm_buffer);
        }
        else
            pos++;
    }

    lily_return_unit(s);
}

/**
method File.open(path: String, mode: String):File

Attempt to open `path` using the `mode` given. `mode` may be one of the
following:

* `"r"` (read only, must exist)
* `"w"` (write only)
* `"a"` (append, create if not exist)
* `"r+"` (read+write, must exist)
* `"w+"` (read+write, creates an empty file if needed)
* `"a+"` (read+append)

# Errors

* `IOError` if unable to open `path`, or an invalid `mode` is provided.
*/
void lily_builtin_File_open(lily_state *s)
{
    char *path = lily_arg_string_raw(s, 0);
    char *mode = lily_arg_string_raw(s, 1);

    errno = 0;
    int ok;

    {
        char *mode_ch = mode;
        if (*mode_ch == 'r' || *mode_ch == 'w' || *mode_ch == 'a') {
            mode_ch++;
            if (*mode_ch == 'b')
                mode_ch++;

            if (*mode_ch == '+')
                mode_ch++;

            ok = (*mode_ch == '\0');
        }
        else
            ok = 0;
    }

    if (ok == 0)
        lily_IOError(s, "Invalid mode '%s' given.", mode);

    FILE *f = fopen(path, mode);
    if (f == NULL) {
        /* Assume that the message is of a reasonable sort of size. */
        char buffer[128];
#ifdef _WIN32
        strerror_s(buffer, sizeof(buffer), errno);
#else
        strerror_r(errno, buffer, sizeof(buffer));
#endif
        lily_IOError(s, "Errno %d: %s (%s).", errno, buffer, path);
    }

    lily_file_val *filev = lily_new_file(f, mode);

    lily_return_file(s, filev);
}

void lily_builtin_File_write(lily_state *);

/**
method File.print[A](self: File, data: A)

Attempt to write the contents of `data` to the file provided. `data` is written
with a newline at the end.

# Errors

* `IOError` if `self` is closed or is not open for writing.
*/
void lily_builtin_File_print(lily_state *s)
{
    lily_builtin_File_write(s);
    fputc('\n', lily_file_for_write(s, lily_arg_file(s, 0)));
    lily_return_unit(s);
}

static lily_bytestring_val *new_sv_take(char *buffer)
{
    lily_bytestring_val *sv = lily_malloc(sizeof(lily_string_val));
    sv->refcount = 0;
    sv->string = buffer;
    sv->size = strlen(buffer);
    return sv;
}

/**
method File.read(self: File, size: *Integer=-1): ByteString

Read `size` bytes from `self`. If `size` is negative, then the full contents of
`self` are read. This stops if either `size` bytes are read, or the end of
`self` is reached.

# Errors:

* `IOError` if `self` is not open for reading, or is closed.
*/
void lily_builtin_File_read(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s,0);
    FILE *raw_file = lily_file_for_read(s, filev);
    int need = -1;
    if (lily_arg_count(s) == 2)
        need = lily_arg_integer(s, 1);

    /* For simplicity, reduce all negative arguments to -1. */
    if (need < -1)
        need = -1;

    size_t bufsize = 64;
    char *buffer = lily_malloc(bufsize);
    int pos = 0, nread;
    int nbuf = bufsize/2;

    while (1) {
        int to_read;
        /* Read either the max possible, or the rest that's needed. */
        if (need == -1 || need > nbuf)
            to_read = nbuf;
        else
            to_read = need;

        nread = fread(buffer+pos, 1, to_read, raw_file);
        pos += nread;

        if (pos >= bufsize) {
            nbuf = bufsize;
            bufsize *= 2;
            buffer = lily_realloc(buffer, bufsize);
        }

        /* Done if EOF hit (first), or got what was wanted (second). */
        if (nread < to_read || (pos >= need && need != -1)) {
            buffer[pos] = '\0';
            break;
        }
        else if (to_read != -1)
            to_read -= nread;
    }

    lily_return_bytestring(s, new_sv_take(buffer));
}

/**
method File.read_line(self: File): ByteString

Attempt to read a line of text from `self`. Currently, this function does not
have a way to signal that the end of the file has been reached. For now, callers
should check the result against `B""`. This will be fixed in a future release.

# Errors

* `IOError` if `self` is not open for reading, or is closed.
*/
void lily_builtin_File_read_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_msgbuf *vm_buffer = lily_get_clean_msgbuf(s);
    char read_buffer[128];
    int ch = 0, pos = 0, total_pos = 0;

    FILE *f = lily_file_for_read(s, filev);

    /* This uses fgetc in a loop because fgets may read in \0's, but doesn't
       tell how much was written. */
    while (1) {
        ch = fgetc(f);

        if (ch == EOF)
            break;

        if (pos == sizeof(read_buffer)) {
            lily_mb_add_slice(vm_buffer, read_buffer, 0, sizeof(read_buffer));
            total_pos += pos;
            pos = 0;
        }

        read_buffer[pos] = (char)ch;
        pos++;

        /* \r is intentionally not checked for, because it's been a very, very
           long time since any os used \r alone for newlines. */
        if (ch == '\n')
            break;
    }

    if (pos != 0) {
        lily_mb_add_slice(vm_buffer, read_buffer, 0, pos);
        total_pos += pos;
    }

    const char *text = lily_mb_get(vm_buffer);
    lily_return_bytestring(s, lily_new_bytestring_sized(text, total_pos));
}

/**
method File.write[A](self: File, data: A)

Attempt to write the contents of `data` to the file provided.

# Errors

* If `self` is closed or is not open for writing, `IOError` is raised.
*/
void lily_builtin_File_write(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_value *to_write = lily_arg_value(s, 1);

    FILE *inner_file = lily_file_for_write(s, filev);

    if (to_write->class_id == LILY_STRING_ID)
        fputs(to_write->value.string->string, inner_file);
    else {
        lily_msgbuf *msgbuf = s->vm_buffer;
        lily_mb_flush(msgbuf);
        lily_mb_add_value(msgbuf, s, to_write);
        fputs(lily_mb_get(msgbuf), inner_file);
    }

    lily_return_unit(s);
}

/**
class Function

The `Function` class represents a block of code to be called, which may or may
not produce a value. `Function` values are first-class, and can be passed around
as arguments, placed into a `List`, and so on.

The arguments of a `Function` are denoted within parentheses, with an optional
colon at the end to denote the value returned:

`Function(Integer): String` (return `String`).

`Function(String, String)` (no value returned).
*/

/**
method Function.doc(self: Function(1)): String

If `self` has a docstring, then the docstring is returned. Otherwise, an empty
`String` is returned instead.
*/
void lily_builtin_Function_doc(lily_state *s)
{
    lily_function_val *f = lily_arg_function(s, 0);
    const char *doc = "";

    if (f->docstring)
        doc = f->docstring;

    lily_return_string(s, lily_new_string(doc));
}

/**
class Hash

The `Hash` class provides a mapping between a key and a value. `Hash` values can
be created through `[key1 => value1, key2 => value2, ...]`. When writing a
`Hash`, the key is the first type, and the value is the second.
/
`[1 => "a", 2 => "b", 3 => "c"]` would therefore be written as
`Hash[Integer, String]`.

Currently, only `Integer` and `String` can be used as keys.
*/

static inline void remove_key_check(lily_state *s, lily_hash_val *hash_val)
{
    if (hash_val->iter_count)
        lily_RuntimeError(s, "Cannot remove key from hash during iteration.");
}

static void destroy_hash_elems(lily_hash_val *hash_val)
{
    int i;
    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];
        lily_hash_entry *next_entry;
        while (entry) {
            lily_deref(entry->boxed_key);
            lily_free(entry->boxed_key);

            lily_deref(entry->record);
            lily_free(entry->record);

            next_entry = entry->next;
            lily_free(entry);
            entry = next_entry;
        }

        hash_val->bins[i] = NULL;
    }
}

void lily_destroy_hash(lily_value *v)
{
    lily_hash_val *hv = v->value.hash;

    destroy_hash_elems(hv);

    lily_free(hv->bins);
    lily_free(hv);
}

/**
method Hash.clear[A, B](self: Hash[A, B])

Removes all pairs currently present within `self`.

# Errors

* `RuntimeError` if `self` is currently being iterated over.
*/
void lily_builtin_Hash_clear(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    if (hash_val->iter_count != 0)
        lily_RuntimeError(s, "Cannot remove key from hash during iteration.");

    destroy_hash_elems(hash_val);

    hash_val->num_entries = 0;

    lily_return_unit(s);
}

/**
method Hash.delete[A, B](self: Hash[A, B], key: A)

Attempt to remove `key` from `self`. If `key` is not present within `self`, then
nothing happens.

# Errors

* `RuntimeError` if `self` is currently being iterated over.
*/
void lily_builtin_Hash_delete(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    remove_key_check(s, hash_val);

    lily_value *key = lily_arg_value(s, 1);
    lily_value *v;

    if (lily_hash_delete(hash_val, &key, &v)) {
        if (key->flags & VAL_IS_DEREFABLE)
            lily_deref(key);

        lily_free(key);

        if (v->flags & VAL_IS_DEREFABLE)
            lily_deref(v);

        lily_free(v);
    }

    lily_return_unit(s);
}

/**
method Hash.each_pair[A, B](self: Hash[A, B], fn: Function(A, B))

Iterate through each pair that is present within `self`. For each of the pairs,
call `fn` with the key and value of each pair.
*/
void lily_builtin_Hash_each_pair(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(s->raiser);
    if (setjmp(link->jump) == 0) {
        int i;
        for (i = 0;i < hash_val->num_bins;i++) {
            lily_hash_entry *entry = hash_val->bins[i];
            while (entry) {
                lily_push_value(s, entry->boxed_key);
                lily_push_value(s, entry->record);
                lily_call_exec_prepared(s, 2);

                entry = entry->next;
            }
        }

        hash_val->iter_count--;
        lily_release_jump(s->raiser);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(s->raiser);
    }
}

/**
method Hash.get[A, B](self: Hash[A, B], key: A, default: B): B

Attempt to find `key` within `self`. If `key` is present, then the value
associated with it is returned. If `key` cannot be found, then `default` is
returned instead.
*/
void lily_builtin_Hash_get(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);
    lily_value *default_value = lily_arg_value(s, 2);

    lily_value *v = lily_hash_find_value(hash_val, key);
    if (v == NULL)
        v = default_value;

    lily_return_value(s, v);
}

/**
method Hash.has_key[A, B](self: Hash[A, B], key: A):Boolean

Return `true` if `key` is present within `self`, `false` otherwise.
*/
void lily_builtin_Hash_has_key(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);

    lily_value *entry = lily_hash_find_value(hash_val, key);

    lily_return_boolean(s, entry != NULL);
}

/**
method Hash.keys[A, B](self: Hash[A, B]): List[A]

Construct a `List` containing all values that are present within `self`. There
is no guarantee of the ordering of the resulting `List`.
*/
void lily_builtin_Hash_keys(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_container_val *result_lv = lily_new_list(hash_val->num_entries);
    int i, list_i;

    for (i = 0, list_i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];
        while (entry) {
            lily_value_assign(result_lv->values[list_i], entry->boxed_key);
            list_i++;
            entry = entry->next;
        }
    }

    lily_return_list(s, result_lv);
}

static lily_hash_val *build_hash(lily_state *s, lily_hash_val *hash_val,
        int count)
{
    int i;

    for (i = 0;i < count;i++) {
        lily_value *record = lily_take_value(s);
        lily_value *key = lily_take_value(s);

        lily_hash_insert_value(hash_val, key, record);
    }

    return hash_val;
}

/**
method Hash.map_values[A, B, C](self: Hash[A, B], Function(B => C)): Hash[A, C]

This iterates through `self` and calls `fn` for each element present. The result
of this function is a newly-made `Hash` where each value is the result of the
call to `fn`.
*/
void lily_builtin_Hash_map_values(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    int count = 0;

    lily_call_prepare(s, lily_arg_function(s, 1));
    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(s->raiser);

    if (setjmp(link->jump) == 0) {
        int i;
        for (i = 0;i < hash_val->num_bins;i++) {
            lily_hash_entry *entry = hash_val->bins[i];
            while (entry) {
                lily_push_value(s, entry->boxed_key);
                lily_push_value(s, entry->record);

                lily_call_exec_prepared(s, 1);

                lily_push_value(s, lily_result_value(s));
                count++;

                entry = entry->next;
            }
        }

        lily_hash_val *result_hash = lily_new_hash_like_sized(hash_val, count);
        build_hash(s, result_hash, count);
        hash_val->iter_count--;
        lily_release_jump(s->raiser);
        lily_return_hash(s, result_hash);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(s->raiser);
    }
}

/**
method Hash.merge[A, B](self: Hash[A, B], others: Hash[A, B]...): Hash[A, B]

Create a new `Hash` that holds the result of `self` and each `Hash present
within `others`.

When duplicate elements are found, the value of the right-most `Hash` wins.
*/
void lily_builtin_Hash_merge(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_hash_val *result_hash = lily_new_hash_like_sized(hash_val,
            hash_val->num_entries);

    int i, j;

    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];
        while (entry) {
            lily_hash_insert_value(result_hash, entry->boxed_key,
                    entry->record);
            entry = entry->next;
        }
    }

    lily_container_val *to_merge = lily_arg_container(s, 1);
    for (i = 0;i < to_merge->num_values;i++) {
        lily_hash_val *merging_hash = to_merge->values[i]->value.hash;
        for (j = 0;j < merging_hash->num_bins;j++) {
            lily_hash_entry *entry = merging_hash->bins[j];
            while (entry) {
                lily_hash_insert_value(result_hash, entry->boxed_key,
                        entry->record);
                entry = entry->next;
            }
        }
    }

    lily_return_hash(s, result_hash);
}

static void hash_select_reject_common(lily_state *s, int expect)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    int count = 0;

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(s->raiser);

    if (setjmp(link->jump) == 0) {
        int i;
        for (i = 0;i < hash_val->num_bins;i++) {
            lily_hash_entry *entry = hash_val->bins[i];
            while (entry) {
                lily_push_value(s, entry->boxed_key);
                lily_push_value(s, entry->record);

                lily_push_value(s, entry->boxed_key);
                lily_push_value(s, entry->record);

                lily_call_exec_prepared(s, 2);
                if (lily_result_boolean(s) != expect) {
                    lily_pop_value(s);
                    lily_pop_value(s);
                }
                else
                    count++;

                entry = entry->next;
            }
        }

        lily_hash_val *result_hash = lily_new_hash_like_sized(hash_val, count);
        build_hash(s, result_hash, count);
        hash_val->iter_count--;
        lily_release_jump(s->raiser);
        lily_return_hash(s, result_hash);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(s->raiser);
    }
}

/**
method Hash.reject[A, B](self: Hash[A, B], fn: Function(A, B => Boolean)): Hash[A, B]

This calls `fn` for each element present within `self`. The result of this
function is a newly-made `Hash` containing all values for which `fn` returns
`false`.
*/
void lily_builtin_Hash_reject(lily_state *s)
{
    hash_select_reject_common(s, 0);
}

/**
method Hash.select[A, B](self: Hash[A, B], fn: Function(A, B => Boolean)): Hash[A, B]

This calls `fn` for each element present within `self`. The result of this
function is a newly-made `Hash` containing all values for which `fn` returns
`true`.
*/
void lily_builtin_Hash_select(lily_state *s)
{
    hash_select_reject_common(s, 1);
}

/**
method Hash.size[A, B](self: Hash[A, B]): Integer

Returns the number of key+value pairs present within `self`.
*/
void lily_builtin_Hash_size(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_return_integer(s, hash_val->num_entries);
}

/**
native IndexError < Exception

The `DivisionByZeroError` is a subclass of `Exception` that is raised when
trying to divide or modulo by zero.
*/

/**
constructor IndexError(m: String): IndexError
*/
void lily_builtin_IndexError_new(lily_state *s)
{
    return_exception(s, LILY_INDEXERROR_ID);
}

/**
class Integer

The `Integer` class is Lily's native numeric type. Internally, it is a wrapper
over a C int64_t.
*/

/**
method Integer.to_bool(self: Integer): Boolean

Converts an `Integer` to a `Boolean`.
*/
void lily_builtin_Integer_to_bool(lily_state *s)
{
    /* Use !! or `x == true` will fail. */
    lily_return_boolean(s, !!lily_arg_integer(s, 0));
}

/**
method Integer.to_byte(self: Integer): Byte

Convert an `Integer` to a `Byte`, truncating the value if necessary.
*/
void lily_builtin_Integer_to_byte(lily_state *s)
{
    lily_return_byte(s, lily_arg_integer(s, 0) & 0xFF);
}

/**
method Integer.to_d(self: Integer): Double

Converts an `Integer` to a `Double`. Internally, this is done by a typecast to
the `Double` type (a raw C double).
*/
void lily_builtin_Integer_to_d(lily_state *s)
{
    double doubleval = (double)lily_arg_integer(s, 0);

    lily_return_double(s, doubleval);
}

/**
method Integer.to_s(self: Integer): String

Convert an `Integer` to a `String` using base-10.
*/
void lily_builtin_Integer_to_s(lily_state *s)
{
    int64_t integer_val = lily_arg_integer(s, 0);

    char buffer[32];
    snprintf(buffer, 32, "%"PRId64, integer_val);

    lily_return_string(s, lily_new_string(buffer));
}

/**
native IOError < Exception

`IOError` is a subclass of `Exception` that is raised when an IO operation fails
or does not have permission.
*/

/**
constructor IOError(m: String): IOError
*/
void lily_builtin_IOError_new(lily_state *s)
{
    return_exception(s, LILY_IOERROR_ID);
}

/**
native KeyError < Exception

`KeyError` is a subclass of `Exception` that is raised when trying to get an
item from a `Hash` that does not exist.
*/

/**
constructor KeyError(m: String): KeyError
*/
void lily_builtin_KeyError_new(lily_state *s)
{
    return_exception(s, LILY_KEYERROR_ID);
}

/**
class List

The `List` class represents a container of a given type, written as
`List[<inner type>]`. A `List` value can be accessed through a positive index or
a negative one (with negative indexes being an offset from the end). Attempting
to access an invalid index will produce `IndexError`.
*/

/**
method List.clear[A](self: List[A])

Removes all elements present within `self`. No error is raised if `self` is
being iterated over.
*/
void lily_builtin_List_clear(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_deref(list_val->values[i]);
        lily_free(list_val->values[i]);
    }

    list_val->extra_space += list_val->num_values;
    list_val->num_values = 0;

    lily_return_unit(s);
}

/**
method List.count[A](self: List[A], fn: Function(A => Boolean)): Integer

This calls `fn` for each element within `self`. The result of this function is
the number of times that `fn` returns `true`.
*/
void lily_builtin_List_count(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    int count = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->values[i]);
        lily_call_exec_prepared(s, 1);

        if (lily_result_boolean(s) == 1)
            count++;
    }

    lily_return_integer(s, count);
}

/* This expands the list value so there's more extra space. Growth is done
   relative to the current size of the list, because why not? */
static void make_extra_space_in_list(lily_container_val *lv)
{
    /* There's probably room for improvement here, later on. */
    int extra = (lv->num_values + 8) >> 2;
    lv->values = lily_realloc(lv->values,
            (lv->num_values + extra) * sizeof(lily_value *));
    lv->extra_space = extra;
}

static int64_t get_relative_index(lily_state *s, lily_container_val *list_val,
        int64_t pos)
{
    if (pos < 0) {
        uint64_t unsigned_pos = -(int64_t)pos;
        if (unsigned_pos > list_val->num_values)
            lily_IndexError(s, "Index %d is too small for list (minimum: %d)",
                    pos, -(int64_t)list_val->num_values);

        pos = list_val->num_values - unsigned_pos;
    }
    else if (pos > list_val->num_values)
        lily_IndexError(s, "Index %d is too large for list (maximum: %d)", pos,
                list_val->num_values);

    return pos;
}

/**
method List.delete_at[A](self: List[A], index: Integer)

Attempts to remove index from the List. If index is negative, then it is
considered an offset from the end of the List.

# Errors

* `IndexError` if `index` is out of range.
*/
void lily_builtin_List_delete_at(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    int64_t pos = lily_arg_integer(s, 1);

    if (list_val->num_values == 0)
        lily_IndexError(s, "Cannot delete from an empty list.");

    pos = get_relative_index(s, list_val, pos);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    lily_value *to_delete = list_val->values[pos];
    lily_deref(to_delete);
    lily_free(to_delete);

    /* Shove everything leftward hide the hole from erasing the value. */
    if (pos != list_val->num_values)
        memmove(list_val->values + pos, list_val->values + pos + 1,
                (list_val->num_values - pos) * sizeof(lily_value *));

    list_val->num_values--;
    list_val->extra_space++;
}

/**
method List.each[A](self: List[A], fn: Function(A)): List[A]

Calls `fn` for each element within `self`. The result of this function is
`self`, so that this method can be chained with others.
*/
void lily_builtin_List_each(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->values[i]);
        lily_call_exec_prepared(s, 1);
    }

    lily_return_list(s, list_val);
}

/**
method List.each_index[A](self: List[A], fn: Function(Integer)): List[A]

Calls `fn` for each element within `self`. Rather than receive the elements of
`self`, `fn` instead receives the index of each element.
*/
void lily_builtin_List_each_index(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_integer(s, i);
        lily_call_exec_prepared(s, 1);
    }

    lily_return_list(s, list_val);
}

/**
method List.fold[A](self: List[A], start: A, fn: Function(A, A => A)): A

This calls `fn` for each element present within `self`. The first value sent to
`fn` is initially `start`, but will later be the result of `fn`. Therefore, the
value as it accumulates can be found in the first value sent to `fn`.

The result of this function is the result of doing an accumulation on each
element within `self`.
*/
void lily_builtin_List_fold(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_value *start = lily_arg_value(s, 1);

    if (list_val->num_values == 0)
        lily_return_value(s, start);
    else {
        lily_value *v = NULL;

        lily_call_prepare(s, lily_arg_function(s, 2));
        lily_push_value(s, start);
        int i = 0;
        while (1) {
            lily_push_value(s, list_val->values[i]);
            lily_call_exec_prepared(s, 2);
            v = lily_result_value(s);

            if (i == list_val->num_values - 1)
                break;

            lily_push_value(s, v);

            i++;
        }

        lily_return_value(s, v);
    }
}

/**
method List.insert[A](self: List[A], index: Integer, value: A)

Attempt to insert `value` at `index` within `self`. If index is negative, then
it is treated as an offset from the end of `self`.

# Errors

* `IndexError` if `index` is not within `self`.
*/
void lily_builtin_List_insert(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    int64_t insert_pos = lily_arg_integer(s, 1);
    lily_value *insert_value = lily_arg_value(s, 2);

    insert_pos = get_relative_index(s, list_val, insert_pos);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    /* Shove everything rightward to make space for the new value. */
    if (insert_pos != list_val->num_values)
        memmove(list_val->values + insert_pos + 1, list_val->values + insert_pos,
                (list_val->num_values - insert_pos) * sizeof(lily_value *));

    list_val->values[insert_pos] = lily_value_copy(insert_value);
    list_val->num_values++;
    list_val->extra_space--;

    lily_return_unit(s);
}

/**
method List.join[A](self: List[A], separator: *String=""): String

Create a `String` consisting of the elements of `self` interleaved with
`separator`. The elements of self are converted to a `String` as if they were
interpolated. If `self` is empty, then the result is an empty `String`.
*/
void lily_builtin_List_join(lily_state *s)
{
    lily_container_val *lv = lily_arg_container(s, 0);
    const char *delim = "";
    if (lily_arg_count(s) == 2)
        delim = lily_arg_string_raw(s, 1);

    lily_msgbuf *vm_buffer = s->vm_buffer;
    lily_mb_flush(vm_buffer);

    if (lv->num_values) {
        int i, stop = lv->num_values - 1;
        lily_value **values = lv->values;
        for (i = 0;i < stop;i++) {
            lily_mb_add_value(vm_buffer, s, values[i]);
            lily_mb_add(vm_buffer, delim);
        }
        if (stop != -1)
            lily_mb_add_value(vm_buffer, s, values[i]);
    }

    lily_return_string(s, lily_new_string(lily_mb_get(vm_buffer)));
}

/**
method List.map[A,B](self: List[A], fn: Function(A => B)): List[B]

This calls `fn` on each element within `self`. The result of this function is a
newly-made `List` containing the results of `fn`.
*/
void lily_builtin_List_map(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *e = list_val->values[i];
        lily_push_value(s, e);
        lily_call_exec_prepared(s, 1);
        lily_push_value(s, lily_result_value(s));
    }

    lily_container_val *result_list = lily_new_list(i);

    i--;
    for (;i >= 0;i--)
        lily_value_assign(result_list->values[i], lily_take_value(s));

    lily_return_list(s, result_list);
}

/**
method List.pop[A](self: List[A]): A

Attempt to remove and return the last element within `self`.

# Errors

* `IndexError` if `self` is empty.
*/
void lily_builtin_List_pop(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    if (list_val->num_values == 0)
        lily_IndexError(s, "Pop from an empty list.");

    lily_value *source = list_val->values[list_val->num_values - 1];

    /* This is a special case because the value is moving out of the list, so
       don't let it get a ref increase. */
    lily_return_value_noref(s, source);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->values[list_val->num_values - 1]);
    list_val->num_values--;
    list_val->extra_space++;
}

/**
method List.push[A](self: List[A], value: A)

Add `value` to the end of `self`.
*/
void lily_builtin_List_push(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_value *insert_value = lily_arg_value(s, 1);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    int value_count = list_val->num_values;

    list_val->values[value_count] = lily_value_copy(insert_value);
    list_val->num_values++;
    list_val->extra_space--;

    lily_return_unit(s);
}

static void list_select_reject_common(lily_state *s, int expect)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));

    int n = 0;
    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->values[i]);
        lily_call_exec_prepared(s, 1);

        int ok = lily_result_boolean(s) == expect;

        if (ok) {
            lily_push_value(s, list_val->values[i]);
            n++;
        }
    }

    lily_container_val *result_list = lily_new_list(n);

    n--;
    for (;n >= 0;n--)
        lily_value_assign(result_list->values[n], lily_take_value(s));

    lily_return_list(s, result_list);
}

/**
method List.reject[A](self: List[A], fn: Function(A => Boolean)): List[A]

This calls `fn` for each element within `self`. The result is a newly-made
`List` holding each element where `fn` returns `false`.
*/
void lily_builtin_List_reject(lily_state *s)
{
    list_select_reject_common(s, 0);
}

/**
method List.repeat[A](count: Integer, value: A): List[A]

This createa a new `List` that contains `value` repeated `count` times.

# Errors

* `ValueError` if `count` is less than 1.
*/
void lily_builtin_List_repeat(lily_state *s)
{
    int n = lily_arg_integer(s, 0);
    if (n < 0)
        lily_ValueError(s, "Repeat count must be >= 0 (%d given).", n);

    lily_value *to_repeat = lily_arg_value(s, 1);
    lily_container_val *lv = lily_new_list(n);

    int i;
    for (i = 0;i < n;i++)
        lily_value_assign(lv->values[i], to_repeat);

    lily_return_list(s, lv);
}

/**
method List.select[A](self: List[A], fn: Function(A => Boolean)): List[A]

This calls `fn` for each element within `self`. The result is a newly-made
`List` holding each element where `fn` returns `true`.
*/
void lily_builtin_List_select(lily_state *s)
{
    list_select_reject_common(s, 1);
}

/**
method List.size[A](self: List[A]): Integer

Returns the number of elements that are within `self`.
*/
void lily_builtin_List_size(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    lily_return_integer(s, list_val->num_values);
}

/**
method List.shift[A](self: List[A]): A

This attempts to remove the last element from `self` and return it.

# Errors

* `ValueError` if `self` is empty.
*/
void lily_builtin_List_shift(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    if (list_val->num_values == 0)
        lily_IndexError(s, "Shift on an empty list.");

    lily_value *source = list_val->values[0];

    /* Similar to List.pop, the value is being taken out so use this custom
       assign to keep the refcount the same. */
    lily_return_value_noref(s, source);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->values[0]);

    if (list_val->num_values != 1)
        memmove(list_val->values, list_val->values + 1,
                (list_val->num_values - 1) *
                sizeof(lily_value *));

    list_val->num_values--;
    list_val->extra_space++;
}

/**
method List.slice[A](self: List[A], start: *Integer=0, stop: *Integer=-1): List[A]

Create a new `List` copying a section of `self` from `start` to `stop`.

If a negative index is given, it is treated as an offset from the end of `self`,
with `-1` being considered the last element.

On error, this generates an empty `List`. Error conditions are:

* Either `start` or `stop` is out of range.
* The `start` is larger than the `stop` (reversed).
*/
void lily_builtin_List_slice(lily_state *s)
{
    lily_container_val *lv = lily_arg_container(s, 0);
    int start = 0;
    int size = lily_container_num_values(lv);
    int stop = size;

    switch (lily_arg_count(s)) {
        case 3: stop = lily_arg_integer(s, 2);
        case 2: start = lily_arg_integer(s, 1);
    }

    if (stop < 0)
        stop = size + stop;
    if (start < 0)
        start = size + start;

    if (stop > size ||
        start > size ||
        start > stop) {
        lily_return_list(s, lily_new_list(0));
        return;
    }

    int new_size = (stop - start);
    lily_container_val *new_lv = lily_new_list(new_size);
    int i, j;

    for (i = 0, j = start;i < new_size;i++, j++) {
        lily_nth_set(new_lv, i, lily_nth_get(lv, j));
    }

    lily_return_list(s, new_lv);
}

/**
method List.unshift[A](self: List[A], value: A)

Inserts value at the front of self, moving all other elements to the right.
*/
void lily_builtin_List_unshift(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_value *input_reg = lily_arg_value(s, 1);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    if (list_val->num_values != 0)
        memmove(list_val->values + 1, list_val->values,
                list_val->num_values * sizeof(lily_value *));

    list_val->values[0] = lily_value_copy(input_reg);

    list_val->num_values++;
    list_val->extra_space--;
}

/**
enum Option[A]
    Some(A)
    None

The `Option` type presents a way to hold either a value of `A`, or `None`, with
`None` being valid for any `Option`. A common use for this is as a return type
for functions that may fail, but have no meaningful error message.
*/

/**
method Option.and[A, B](self: Option[A], other: Option[B]): Option[B]

If `self` is a `Some`, this returns `other`.

Otherwise, this returns `None`.
*/
void lily_builtin_Option_and(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 1));
    else
        lily_return_value(s, lily_arg_value(s, 0));
}

/**
method Option.and_then[A, B](self: Option[A], fn: Function(A => Option[B])): Option[B]

If `self` is a `Some`, this calls `fn` with the value within the `Some`. The
result is the result of the `Option` returned by `fn`.

Otherwise, this returns `None`.
*/
void lily_builtin_Option_and_then(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_push_value(s, lily_arg_nth_get(s, 0, 0));

        lily_call_simple(s, lily_arg_function(s, 1), 1);

        lily_return_value(s, lily_result_value(s));
    }
    else
        lily_return_none(s);
}

/**
method Option.is_none[A](self: Option[A]): Boolean

If `self` is a `Some`, this returns `false`.

Otherwise, this returns `true`.
*/
void lily_builtin_Option_is_none(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_some(s, 0) == 0);
}

/**
method Option.is_some[A](self: Option[A]): Boolean

If `self` is a `Some`, this returns `true`.

Otherwise, this returns `false`.
*/
void lily_builtin_Option_is_some(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_some(s, 0));
}

/**
method Option.map[A, B](self: Option[A], fn: Function(A => B)): Option[B]

If `self` is a `Some`, this returns a `Some` holding the result of `fn`.

Otherwise, this returns `None`.
*/
void lily_builtin_Option_map(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_push_value(s, lily_arg_nth_get(s, 0, 0));

        lily_call_simple(s, lily_arg_function(s, 1), 1);

        lily_container_val *variant = lily_new_some();
        lily_nth_set(variant, 0, lily_result_value(s));
        lily_return_variant(s, variant);
    }
    else
        lily_return_none(s);
}

/**
method Option.or[A](self: Option[A], alternate: Option[A]): Option[A]

If `self` is a `Some`, this returns `self`.

Otherwise, this returns `alternate`.
*/
void lily_builtin_Option_or(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 0));
    else
        lily_return_value(s, lily_arg_value(s, 1));
}

/**
method Option.or_else[A](self: Option[A], fn: Function( => Option[A])):Option[A]

If `self` is a `Some`, this returns `self`.

Otherwise, this returns the result of calling `fn`.
*/
void lily_builtin_Option_or_else(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 0));
    else {
        lily_call_simple(s, lily_arg_function(s, 1), 0);

        lily_return_value(s, lily_result_value(s));
    }
}

/**
method Option.unwrap[A](self: Option[A]): A

If `self` is a `Some`, this returns the value contained within.

# Errors

* `ValueError` if `self` is `None`.
*/
void lily_builtin_Option_unwrap(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_nth_get(s, 0, 0));
    else
        lily_ValueError(s, "unwrap called on None.");
}

/**
method Option.unwrap_or[A](self: Option[A], alternate: A):A

If `self` is a `Some`, this returns the value with `self`.

Otherwise, this returns `alternate`.
*/
void lily_builtin_Option_unwrap_or(lily_state *s)
{
    lily_value *source;

    if (lily_arg_is_some(s, 0))
        source = lily_arg_nth_get(s, 0, 0);
    else
        source = lily_arg_value(s, 1);

    lily_return_value(s, source);
}

/**
method Option.unwrap_or_else[A](self: Option[A], fn: Function( => A)):A

If `self` is a `Some`, this returns the value with `self`.

Otherwise, this returns the result of calling `fn`.
*/
void lily_builtin_Option_unwrap_or_else(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_nth_get(s, 0, 0));
    else {
        lily_call_simple(s, lily_arg_function(s, 1), 0);

        lily_return_value(s, lily_result_value(s));
    }
}

/**
enum Result[A, B]
    Failure(A)
    Success(B)

`Result` is an enum that holds either a `Failure` or `Success`. This enum is
for situations where the function that fails has an error message to deliver.
Examples of that include a database query or a more humble rpn calculator.
*/
static void result_optionize(lily_state *s, int expect)
{
    if (lily_arg_is_success(s, 0) == expect) {
        lily_container_val *variant = lily_new_some();
        lily_nth_set(variant, 0, lily_arg_nth_get(s, 0, 0));
        lily_return_variant(s, variant);
    }
    else
        lily_return_none(s);
}

/**
method Result.failure[A, B](self: Result[A, B]):Option[A]

If `self` contains a `Failure`, produces a `Some(A)`.

If `self` contains a `Right`, produces `None`.
*/
void lily_builtin_Result_failure(lily_state *s)
{
    result_optionize(s, 0);
}

static void result_is_success_or_failure(lily_state *s, int expect)
{
    lily_return_boolean(s, lily_arg_is_success(s, 0) == expect);
}

/**
method Result.is_failure[A, B](self: Result[A, B]): Boolean

Return `true` if `self` contains a `Failure`, `false` otherwise.
*/
void lily_builtin_Result_is_failure(lily_state *s)
{
    result_is_success_or_failure(s, 0);
}

/**
method Result.is_success[A, B](self: Result[A, B]): Boolean

Return `true` if `self` contains a `Success`, `false` otherwise.
*/
void lily_builtin_Result_is_success(lily_state *s)
{
    result_is_success_or_failure(s, 1);
}

/**
method Result.success[A, B](self: Result[A, B]): Option[B]

If `self` contains a `Failure`, produces a `None`.

If `self` contains a `Success`, produces `Right(B)`.
*/
void lily_builtin_Result_success(lily_state *s)
{
    result_optionize(s, 1);
}

/**
native RuntimeError < Exception

`RuntimeError` is a subclass of `Exception` that is raised when the recursion
limit is exceeded, or when trying to modify a `Hash` while iterating over it.
*/

/**
constructor RuntimeError(m: String): RuntimeError
*/
void lily_builtin_RuntimeError_new(lily_state *s)
{
    return_exception(s, LILY_RUNTIMEERROR_ID);
}

/**
class String

The `String` class provides a wrapper over a C char *. The `String` class is
guaranteed to have a single '\0' terminator. Additionally, a `String` is
guaranteed to always be valid utf-8.

The methods on the `String` class treat the underlying `String` as being
immutable, and thus always create a new `String` instead of modifying the
existing one.
*/

static int char_index(const char *s, int idx, char ch)
{
    const char *P = strchr(s + idx,ch);
    if (P == NULL)
        return -1;
    else
        return (int)((uintptr_t)P - (uintptr_t)s);
}

/**
method String.format(self: String, args: 1...): String

This creates a new `String` by processing `self` as a format. Format specifiers
must be between braces (`{}`), and must be between `0` and `99`. Each format
specifier is replaced with the according argument, with the first argument being
at 0, the second at 1, and so on.

This function is a useful alternative to interpolation for situations where the
value is a long expression, or where a single value is to be repeated several
times.

# Errors

* `ValueError` if a format specifier is malformed or has too many digits.

* `IndexError` if the format specifier specifies an out-of-range argument.
*/
void lily_builtin_String_format(lily_state *s)
{
    const char *fmt = lily_arg_string_raw(s, 0);
    lily_container_val *lv = lily_arg_container(s, 1);

    int lsize = lily_container_num_values(lv);
    lily_msgbuf *msgbuf = lily_get_clean_msgbuf(s);

    int idx, last_idx = 0;

    while (1) {
        idx = char_index(fmt, last_idx, '{');
        if (idx > -1) {
            if (idx > last_idx)
                lily_mb_add_slice(msgbuf, fmt, last_idx, idx);

            char ch;
            int i, total = 0;
            int start = idx + 1;

            /* Ignore any leading zeroes, but cap at 2 digits. */
            do {
                idx++;
                ch = fmt[idx];
            } while (ch == '0');

            for (i = 0;i < 2;i++) {
                if (isdigit(ch) == 0)
                    break;

                total = (total * 10) + (ch - '0');
                idx++;
                ch = fmt[idx];
            }

            if (isdigit(ch))
                lily_ValueError(s, "Format must be between 0...99.");
            else if (start == idx)
                lily_ValueError(s, "Format specifier is empty.");
            else if (ch != '}')
                lily_ValueError(s, "Format specifier is not numeric.");
            else if (total >= lsize)
                lily_IndexError(s, "Format specifier is too large.");

            idx++;
            last_idx = idx;

            lily_value *v = lily_nth_get(lv, total);
            lily_mb_add_value(msgbuf, s, v);
        }
        else {
            lily_mb_add_slice(msgbuf, fmt, last_idx, strlen(fmt));
            break;
        }
    }

    lily_return_string(s, lily_new_string(lily_mb_get(msgbuf)));
}

/**
method String.ends_with(self: String, end: String): Boolean

Checks if `self` ends with `end`.
*/
void lily_builtin_String_ends_with(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *suffix_arg = lily_arg_value(s, 1);

    char *input_raw_str = input_arg->value.string->string;
    char *suffix_raw_str = suffix_arg->value.string->string;
    int input_size = input_arg->value.string->size;
    int suffix_size = suffix_arg->value.string->size;

    if (suffix_size > input_size) {
        lily_return_boolean(s, 0);
        return;
    }

    int input_i, suffix_i, ok = 1;
    for (input_i = input_size - 1, suffix_i = suffix_size - 1;
         suffix_i >= 0;
         input_i--, suffix_i--) {
        if (input_raw_str[input_i] != suffix_raw_str[suffix_i]) {
            ok = 0;
            break;
        }
    }

    lily_return_boolean(s, ok);
}


/**
method String.find(self: String, needle: String, start: *Integer=0): Option[Integer]

Check for `needle` being within `self`. By default, this begins at the start of
`self`. If `start` is non-zero, then the search begins `start` bytes away from
the beginning of `self`. If `start` lies within the middle of a utf-8 codepoint,
then `None` is automatically returned.

If `needle` is found, the result is a `Some` holding the index.

Otherwise, this returns `None`.
*/
void lily_builtin_String_find(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *find_arg = lily_arg_value(s, 1);
    int start = 0;
    if (lily_arg_count(s) == 3)
        start = lily_arg_integer(s, 2);

    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *find_str = find_arg->value.string->string;
    int find_length = find_arg->value.string->size;

    if (find_length > input_length ||
        find_length == 0 ||
        start > input_length ||
        follower_table[(unsigned char)input_str[start]] == -1) {
        lily_return_none(s);
        return;
    }

    char find_ch;
    int i, j, k, length_diff, match;

    length_diff = input_length - find_length;
    find_ch = find_str[0];
    match = 0;

    /* This stops at length_diff for two reasons:
       * The inner loop won't have to do a boundary check.
       * Search will stop if there isn't enough length left for a match
         (ex: "abcdef".find("defg")) */
    for (i = start;i <= length_diff;i++) {
        if (input_str[i] == find_ch) {
            match = 1;
            /* j starts at i + 1 to skip the first match.
               k starts at 1 for the same reason. */
            for (j = i + 1, k = 1;k < find_length;j++, k++) {
                if (input_str[j] != find_str[k]) {
                    match = 0;
                    break;
                }
            }
            if (match == 1)
                break;
        }
    }

    if (match) {
        lily_container_val *variant = lily_new_some();
        lily_nth_set(variant, 0, lily_box_integer(s, i));
        lily_return_variant(s, variant);
    }
    else
        lily_return_none(s);
}

/**
method String.html_encode(self: String): String

Check for one of `"&"`, `"<"`, or `">"` being within `self`.

If found, a new `String` is contained with any instance of the above being
replaced by an html-safe value.

If not found, `self` is returned.
*/
void lily_builtin_String_html_encode(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    const char *raw = lily_value_string_raw(input_arg);
    lily_msgbuf *msgbuf = lily_get_clean_msgbuf(s);

    /* If nothing was escaped, output what was input. */
    if (lily_mb_html_escape(msgbuf, raw) == raw)
        /* The `String` given may be a cached literal, so return the input arg
           instead of making a new `String`. */
        lily_return_value(s, input_arg);
    else
        lily_return_string(s, lily_new_string(lily_mb_get(msgbuf)));
}

#define CTYPE_WRAP(WRAP_NAME, WRAPPED_CALL) \
void lily_builtin_String_##WRAP_NAME(lily_state *s) \
{ \
    lily_string_val *input = lily_arg_string(s, 0); \
    int length = lily_string_length(input); \
\
    if (length == 0) { \
        lily_return_boolean(s, 0); \
        return; \
    } \
\
    const char *loop_str = lily_string_raw(input); \
    int i = 0; \
    int ok = 1; \
\
    for (i = 0;i < length;i++) { \
        if (WRAPPED_CALL(loop_str[i]) == 0) { \
            ok = 0; \
            break; \
        } \
    } \
\
    lily_return_boolean(s, ok); \
}

/**
method String.is_alnum(self: String):Boolean

Return `true` if `self` has only alphanumeric([a-zA-Z0-9]+) characters, `false`
otherwise.
*/
CTYPE_WRAP(is_alnum, isalnum)

/**
method String.is_alpha(self: String):Boolean

Return `true` if `self` has only alphabetical([a-zA-Z]+) characters, `false`
otherwise.
*/
CTYPE_WRAP(is_alpha, isalpha)

/**
method String.is_digit(self: String):Boolean

Return `true` if `self` has only digit([0-9]+) characters, `false` otherwise.
*/
CTYPE_WRAP(is_digit, isdigit)

/**
method String.is_space(self: String):Boolean

Returns `true` if `self` has only space(" \t\r\n") characters, `false`
otherwise.
*/
CTYPE_WRAP(is_space, isspace)

/**
method String.lower(self: String):String

Checks if any characters within `self` are within [A-Z]. If so, it creates a new
`String` with [A-Z] replaced by [a-z]. Otherwise, `self` is returned.
*/
void lily_builtin_String_lower(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    int new_size = input_arg->value.string->size + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    char *new_str = new_sv->string;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;
    int i;

    for (i = 0;i < input_length;i++) {
        char ch = input_str[i];
        if (isupper(ch))
            new_str[i] = tolower(ch);
        else
            new_str[i] = ch;
    }
    new_str[input_length] = '\0';

    lily_return_string(s, new_sv);
}

/* This is a helper for lstrip wherein input_arg has some utf-8 bits inside. */
static int lstrip_utf8_start(lily_value *input_arg, lily_string_val *strip_sv)
{
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *strip_str = strip_sv->string;
    int strip_length = strip_sv->size;
    int i = 0, j = 0, match = 1;

    char ch = strip_str[0];
    if (follower_table[(unsigned char)ch] == strip_length) {
        /* Only a single utf-8 char. This is a bit simpler. */
        char strip_start_ch = ch;
        int char_width = follower_table[(unsigned char)ch];
        while (i < input_length) {
            if (input_str[i] == strip_start_ch) {
                /* j starts at 1 because the first byte was already checked.
                   This compares the inner part of the strip string and the
                   input string to make sure the whole utf-8 chunk matches. */
                for (j = 1;j < char_width;j++) {
                    if (input_str[i + j] != strip_str[j]) {
                        match = 0;
                        break;
                    }
                }
                if (match == 0)
                    break;

                i += char_width;
            }
            else
                break;
        }
    }
    else {
        /* There's at least one utf-8 chunk. There may be ascii bytes to strip
           as well, or more utf-8 chunks. This is the most complicated case. */
        char input_ch;
        int char_width, k;
        while (1) {
            input_ch = input_str[i];
            if (input_ch == strip_str[j]) {
                char_width = follower_table[(unsigned char)strip_str[j]];
                match = 1;
                /* This has to use k, unlike the above loop, because j is being
                   used to hold the current position in strip_str. */
                for (k = 1;k < char_width;k++) {
                    if (input_str[i + k] != strip_str[j + k]) {
                        match = 0;
                        break;
                    }
                }
                if (match == 1) {
                    /* Found a match, so eat the valid utf-8 chunk and start
                       from the beginning of the strip string again. This makes
                       sure that each chunk of the input string is matched to
                       each chunk of the strip string. */
                    i += char_width;
                    if (i >= input_length)
                        break;
                    else {
                        j = 0;
                        continue;
                    }
                }
            }

            /* This assumes that strip_str is valid utf-8. */
            j += follower_table[(unsigned char)strip_str[j]];

            /* If all chunks in the strip str have been checked, then
               everything that can be removed has been removed. */
            if (j == strip_length) {
                match = 0;
                break;
            }
        }
    }

    return i;
}

/* This is a helper for lstrip wherein input_arg does not have utf-8. */
static int lstrip_ascii_start(lily_value *input_arg, lily_string_val *strip_sv)
{
    int i;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    if (strip_sv->size == 1) {
        /* Strip a single byte really fast. The easiest case. */
        char strip_ch;
        strip_ch = strip_sv->string[0];
        for (i = 0;i < input_length;i++) {
            if (input_str[i] != strip_ch)
                break;
        }
    }
    else {
        /* Strip one of many ascii bytes. A bit tougher, but not much. */
        char *strip_str = strip_sv->string;
        int strip_length = strip_sv->size;
        for (i = 0;i < input_length;i++) {
            char ch = input_str[i];
            int found = 0;
            int j;
            for (j = 0;j < strip_length;j++) {
                if (ch == strip_str[j]) {
                    found = 1;
                    break;
                }
            }
            if (found == 0)
                break;
        }
    }

    return i;
}

/**
method String.lstrip(self: String, to_strip: String):String

This walks through `self` from left to right, stopping on the first utf-8 chunk
that is not found within `to_strip`. The result is a newly-made copy of self
without the elements within `to_strip` at the front.
*/
void lily_builtin_String_lstrip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    char *strip_str;
    unsigned char ch;
    int copy_from, i, has_multibyte_char, strip_str_len;
    lily_string_val *strip_sv;

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    strip_sv = strip_arg->value.string;
    strip_str = strip_sv->string;
    strip_str_len = strlen(strip_str);
    has_multibyte_char = 0;

    for (i = 0;i < strip_str_len;i++) {
        ch = (unsigned char)strip_str[i];
        if (ch > 127) {
            has_multibyte_char = 1;
            break;
        }
    }

    if (has_multibyte_char == 0)
        copy_from = lstrip_ascii_start(input_arg, strip_sv);
    else
        copy_from = lstrip_utf8_start(input_arg, strip_sv);

    int new_size = (input_arg->value.string->size - copy_from) + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    strcpy(new_sv->string, input_arg->value.string->string + copy_from);

    lily_return_string(s, new_sv);
}

/**
method String.parse_i(self: String): Option[Integer]

Attempts to convert `self` into an `Integer`. Currently, `self` is parsed as a
base-10 encoded value.

If the value is a valid `Integer`, then a `Some` containing the value is
returned.

Otherwise, `None` is returned.
*/
void lily_builtin_String_parse_i(lily_state *s)
{
    char *input = lily_arg_string_raw(s, 0);
    uint64_t value = 0;
    int is_negative = 0;
    unsigned int rounds = 0;
    int leading_zeroes = 0;

    if (*input == '-') {
        is_negative = 1;
        ++input;
    }
    else if (*input == '+')
        ++input;

    if (*input == '0') {
        ++input;
        leading_zeroes = 1;
        while (*input == '0')
            ++input;
    }

    /* A signed int64 peaks at 9223372036854775807 (or ...808 for negative).
       The maximum number of reasonable digits is therefore 20 for scanning
       decimal. */
    while (*input >= '0' && *input <= '9' && rounds != 20) {
        value = (value * 10) + (*input - '0');
        ++input;
        rounds++;
    }

    /* These cases check for overflow, trailing junk, and just + or just -. */
    if (value > ((uint64_t)INT64_MAX + is_negative) ||
        *input != '\0' ||
        (rounds == 0 && leading_zeroes == 0)) {
        lily_return_none(s);
    }
    else {
        int64_t signed_value;

        if (is_negative == 0)
            signed_value = (int64_t)value;
        else
            signed_value = -(int64_t)value;

        lily_container_val *variant = lily_new_some();
        lily_nth_set(variant, 0, lily_box_integer(s, signed_value));
        lily_return_variant(s, variant);
    }
}

/**
method String.replace(self: String, needle: String, new: String): String

Create a new `String` consisting of every `needle` replaced with `new`.
*/
void lily_builtin_String_replace(lily_state *s)
{
    lily_string_val *source_sv = lily_arg_string(s, 0);
    lily_string_val *needle_sv = lily_arg_string(s, 1);
    int source_len = lily_string_length(source_sv);
    int needle_len = lily_string_length(needle_sv);

    if (needle_len > source_len) {
        lily_return_string(s, source_sv);
        return;
    }

    lily_msgbuf *msgbuf = lily_get_clean_msgbuf(s);
    char *source = lily_string_raw(source_sv);
    char *needle = lily_string_raw(needle_sv);
    char *replace_with = lily_arg_string_raw(s, 2);
    char needle_first = *needle;
    char ch;
    int start = 0;
    int i;

    for (i = 0;i < source_len;i++) {
        ch = source[i];
        if (ch == needle_first &&
            (i + needle_len) <= source_len) {
            int match = 1;
            int j;
            for (j = 1;j < needle_len;j++) {
                if (needle[j] != source[i + j])
                    match = 0;
            }

            if (match) {
                if (i != start)
                    lily_mb_add_slice(msgbuf, source, start, i);

                lily_mb_add(msgbuf, replace_with);
                i += needle_len - 1;
                start = i + 1;
            }
        }
    }

    if (i != start)
        lily_mb_add_slice(msgbuf, source, start, i);

    lily_return_string(s, lily_new_string(lily_mb_get(msgbuf)));
}

/* This is a helper for rstrip when there's no utf-8 in input_arg. */
static int rstrip_ascii_stop(lily_value *input_arg, lily_string_val *strip_sv)
{
    int i;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    if (strip_sv->size == 1) {
        char strip_ch = strip_sv->string[0];
        for (i = input_length - 1;i >= 0;i--) {
            if (input_str[i] != strip_ch)
                break;
        }
    }
    else {
        char *strip_str = strip_sv->string;
        int strip_length = strip_sv->size;
        for (i = input_length - 1;i >= 0;i--) {
            char ch = input_str[i];
            int found = 0;
            int j;
            for (j = 0;j < strip_length;j++) {
                if (ch == strip_str[j]) {
                    found = 1;
                    break;
                }
            }
            if (found == 0)
                break;
        }
    }

    return i + 1;
}

/* Helper for rstrip, for when there is some utf-8. */
static int rstrip_utf8_stop(lily_value *input_arg, lily_string_val *strip_sv)
{
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *strip_str = strip_sv->string;
    int strip_length = strip_sv->size;
    int i, j;

    i = input_length - 1;
    j = 0;
    while (i >= 0) {
        /* First find out how many bytes are in the current chunk. */
        int follow_count = follower_table[(unsigned char)strip_str[j]];
        /* Now get the last byte of this chunk. Since the follower table
           includes the total, offset by -1. */
        char last_strip_byte = strip_str[j + (follow_count - 1)];
        /* Input is going from right to left. See if input matches the last
           byte of the current utf-8 chunk. But also check that there are
           enough chars left to protect against underflow. */
        if (input_str[i] == last_strip_byte &&
            i + 1 >= follow_count) {
            int match = 1;
            int input_i, strip_i, k;
            /* input_i starts at i - 1 to skip the last byte.
               strip_i starts at follow_count so it can stop things. */
            for (input_i = i - 1, strip_i = j + (follow_count - 2), k = 1;
                 k < follow_count;
                 input_i--, strip_i--, k++) {
                if (input_str[input_i] != strip_str[strip_i]) {
                    match = 0;
                    break;
                }
            }

            if (match == 1) {
                i -= follow_count;
                j = 0;
                continue;
            }
        }

        /* Either the first byte or one of the inner bytes didn't match.
           Go to the next chunk and try again. */
        j += follow_count;
        if (j == strip_length)
            break;

        continue;
    }

    return i + 1;
}

/**
method String.rstrip(self: String, to_strip: String):String

This walks through `self` from right to left, stopping on the first utf-8 chunk
that is not found within `to_strip`. The result is a newly-made copy of `self`
without the elements of `to_strip` at the end.
*/
void lily_builtin_String_rstrip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    char *strip_str;
    unsigned char ch;
    int copy_to, i, has_multibyte_char, strip_str_len;
    lily_string_val *strip_sv;

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    strip_sv = strip_arg->value.string;
    strip_str = strip_sv->string;
    strip_str_len = strlen(strip_str);
    has_multibyte_char = 0;

    for (i = 0;i < strip_str_len;i++) {
        ch = (unsigned char)strip_str[i];
        if (ch > 127) {
            has_multibyte_char = 1;
            break;
        }
    }

    if (has_multibyte_char == 0)
        copy_to = rstrip_ascii_stop(input_arg, strip_sv);
    else
        copy_to = rstrip_utf8_stop(input_arg, strip_sv);

    int new_size = copy_to + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    strncpy(new_sv->string, input_arg->value.string->string, copy_to);
    /* This will always copy a partial string, so make sure to add a terminator. */
    new_sv->string[copy_to] = '\0';

    lily_return_string(s, new_sv);
}

static const char move_table[256] =
{
     /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 9 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* A */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* B */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* C */ 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
/* F */ 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static lily_container_val *string_split_by_val(lily_state *s, char *input,
        char *splitby)
{
    char *input_ch = &input[0];
    char *splitby_ch = &splitby[0];
    int values_needed = 0;

    while (move_table[(unsigned char)*input_ch] != 0) {
        if (*input_ch == *splitby_ch) {
            char *restore_ch = input_ch;
            int is_match = 1;
            while (*input_ch == *splitby_ch) {
                splitby_ch++;
                input_ch++;
                if (*splitby_ch == '\0')
                    break;

                if (*input_ch != *splitby_ch) {
                    is_match = 0;
                    input_ch = restore_ch;
                    break;
                }
            }

            splitby_ch = &splitby[0];
            values_needed += is_match;
        }
        else
            input_ch += move_table[(unsigned char)*input_ch];
    }

    values_needed++;
    input_ch = &input[0];
    lily_container_val *list_val = lily_new_list(values_needed);
    int i = 0;
    char *last_start = input_ch;

    while (1) {
        char *match_start = input_ch;
        int is_match = 0;
        if (*input_ch == *splitby_ch) {
            is_match = 1;
            while (*input_ch == *splitby_ch) {
                splitby_ch++;
                if (*splitby_ch == '\0')
                    break;

                input_ch++;
                if (*input_ch != *splitby_ch) {
                    is_match = 0;
                    input_ch = match_start;
                    break;
                }
            }
            splitby_ch = &splitby[0];
        }

        /* The second check is so that if the last bit of the input string
           matches the split string, an empty string will be made.
           Ex: "1 2 3 ".split(" ") # ["1", "2", "3", ""] */
        if (is_match || *input_ch == '\0') {
            int size = match_start - last_start;
            lily_string_val *sv = lily_new_string_sized(last_start, size);
            lily_nth_set(list_val, i, lily_box_string(s, sv));

            i++;
            if (*input_ch == '\0')
                break;

            last_start = input_ch + 1;
        }
        else if (*input_ch == '\0')
            break;

        input_ch++;
    }

    return list_val;
}

/**
method String.slice(self: String, start: *Integer=0, stop: *Integer=-1): String

Create a new `String` copying a section of `self` from `start` to `stop`. This
function works using byte indexes into the `String` value.

If a negative index is given, it is treated as an offset from the end of `self`,
with `-1` being considered the last element.

On error, this generates an empty `String`. Error conditions are:

* Either `start` or `stop` is out of range.
* The resulting slice would not be valid utf-8.
* The `start` is larger than the `stop` (reversed).
*/
void lily_builtin_String_slice(lily_state *s)
{
    do_str_slice(s, 0);
}

/**
method String.split(self: String, split_by: *String=" "):List[String]

This attempts to split `self` using `split_by`, with a default value of a single
space.

# Errors

* `ValueError` if `split_by` is empty.
*/
void lily_builtin_String_split(lily_state *s)
{
    lily_string_val *input_strval = lily_arg_string(s, 0);
    lily_string_val *split_strval;
    lily_string_val fake_sv;

    if (lily_arg_count(s) == 2) {
        split_strval = lily_arg_string(s, 1);
        if (split_strval->size == 0)
            lily_ValueError(s, "Cannot split by empty string.");
    }
    else {
        fake_sv.string = " ";
        fake_sv.size = 1;
        split_strval = &fake_sv;
    }

    lily_container_val *lv = string_split_by_val(s, input_strval->string,
            split_strval->string);

    lily_return_list(s, lv);
}

/**
method String.starts_with(self: String, with: String): Boolean

Checks if `self` starts with `with`.
*/
void lily_builtin_String_starts_with(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *prefix_arg = lily_arg_value(s, 1);

    char *input_raw_str = input_arg->value.string->string;
    char *prefix_raw_str = prefix_arg->value.string->string;
    int prefix_size = prefix_arg->value.string->size;

    if (input_arg->value.string->size < prefix_size) {
        lily_return_boolean(s, 0);
        return;
    }

    int i, ok = 1;
    for (i = 0;i < prefix_size;i++) {
        if (input_raw_str[i] != prefix_raw_str[i]) {
            ok = 0;
            break;
        }
    }

    lily_return_boolean(s, ok);
}

/**
method String.strip(self: String, to_strip: String):String

This walks through self from right to left, and then from left to right. The
result of this is a newly-made `String` without any elements within `to_strip`
at either end.
*/
void lily_builtin_String_strip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    char ch;
    lily_string_val *strip_sv = strip_arg->value.string;
    char *strip_str = strip_sv->string;
    int strip_str_len = strlen(strip_str);
    int has_multibyte_char = 0;
    int copy_from, copy_to, i;

    for (i = 0;i < strip_str_len;i++) {
        ch = (unsigned char)strip_str[i];
        if (ch > 127) {
            has_multibyte_char = 1;
            break;
        }
    }

    if (has_multibyte_char == 0)
        copy_from = lstrip_ascii_start(input_arg, strip_sv);
    else
        copy_from = lstrip_utf8_start(input_arg, strip_sv);

    if (copy_from != input_arg->value.string->size) {
        if (has_multibyte_char)
            copy_to = rstrip_ascii_stop(input_arg, strip_sv);
        else
            copy_to = rstrip_utf8_stop(input_arg, strip_sv);
    }
    else
        /* The whole string consists of stuff in strip_str. Do this so the
           result is an empty string. */
        copy_to = copy_from;

    int new_size = (copy_to - copy_from) + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    char *new_str = new_sv->string;
    strncpy(new_str, input_arg->value.string->string + copy_from, new_size - 1);
    new_str[new_size - 1] = '\0';

    lily_return_string(s, new_sv);
}

/**
method String.to_bytestring(self: String): ByteString

Produce a copy of `self`, as a `ByteString`. This allows per-`Byte` operations
to be performed.
*/
void lily_builtin_String_to_bytestring(lily_state *s)
{
    /* They currently have the same internal representation. This method is
       provided for the type system. */
    lily_return_bytestring(s, (lily_bytestring_val *)lily_arg_string(s, 0));
}

/**
method String.trim(self: String): String

Checks if `self` starts or ends with any of `" \t\r\n"`. If it does, then a new
`String` is made with spaces removed from both sides. If it does not, then this
returns `self`.
*/
void lily_builtin_String_trim(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    char fake_buffer[5] = " \t\r\n";
    lily_string_val fake_sv;
    fake_sv.string = fake_buffer;
    fake_sv.size = strlen(fake_buffer);

    int copy_from = lstrip_ascii_start(input_arg, &fake_sv);
    lily_string_val *new_sv;

    if (copy_from != input_arg->value.string->size) {
        int copy_to = rstrip_ascii_stop(input_arg, &fake_sv);
        int new_size = (copy_to - copy_from) + 1;
        new_sv = make_sv(s, new_size);
        char *new_str = new_sv->string;

        strncpy(new_str, input_arg->value.string->string + copy_from, new_size - 1);
        new_str[new_size - 1] = '\0';
    }
    else {
        /* It's all space, so make a new empty string. */
        new_sv = make_sv(s, 1);
        new_sv->string[0] = '\0';
    }

    lily_return_string(s, new_sv);
}

/**
method String.upper(self: String):String

Checks if any characters within self are within [a-z]. If so, it creates a new
`String` with [a-z] replaced by [A-Z]. Otherwise, `self` is returned.
*/
void lily_builtin_String_upper(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    int new_size = input_arg->value.string->size + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    char *new_str = new_sv->string;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;
    int i;

    for (i = 0;i < input_length;i++) {
        char ch = input_str[i];
        if (islower(ch))
            new_str[i] = toupper(ch);
        else
            new_str[i] = ch;
    }
    new_str[input_length] = '\0';

    lily_return_string(s, new_sv);
}

/**
class Tuple

The `Tuple` class provides a fixed-size container over a set of types. `Tuple`
is ideal for situations where a variety of data is needed, but a class is too
complex.

`Tuple` literals are created by `<[value1, value2, ...]>`. Member of the `Tuple`
class can be accessed through subscripts. Unlike `List`, `Tuple` does not
support negative indexes.

Operations on the `Tuple` class use the types `1` and `2`. These are special
types that match against any number of types. This allows `Tuple` operations to
work on all `Tuple` instances, regardless of the number of elements within the
`Tuple` (sometimes considered its arity).
*/


/**
method Tuple.merge(self: Tuple[1], other: Tuple[2]): Tuple[1, 2]

Build a new `Tuple` composed of the contents of `self` and the contents of
`other`.
*/
void lily_builtin_Tuple_merge(lily_state *s)
{
    lily_container_val *left_tuple = lily_arg_container(s, 0);
    lily_container_val *right_tuple = lily_arg_container(s, 1);

    int new_count = left_tuple->num_values + right_tuple->num_values;
    lily_container_val *lv = lily_new_tuple(new_count);

    int i, j;
    for (i = 0, j = 0;i < left_tuple->num_values;i++, j++)
        lily_value_assign(lv->values[j], left_tuple->values[i]);

    for (i = 0;i < right_tuple->num_values;i++, j++)
        lily_value_assign(lv->values[j], right_tuple->values[i]);

    lily_return_tuple(s, lv);
}

/**
method Tuple.push[A](self: Tuple[1], other: A): Tuple[1, A]

Build a new `Tuple` composed of the contents of `self` and `other`.
*/
void lily_builtin_Tuple_push(lily_state *s)
{
    lily_container_val *left_tuple = lily_arg_container(s, 0);
    lily_value *right = lily_arg_value(s, 1);
    lily_container_val *lv = lily_new_tuple(left_tuple->num_values + 1);

    int i, j;
    for (i = 0, j = 0;i < left_tuple->num_values;i++, j++)
        lily_value_assign(lv->values[j], left_tuple->values[i]);

    lily_value_assign(lv->values[j], right);

    lily_return_tuple(s, lv);
}

/**
native ValueError < Exception

`ValueError` is a subclass of `Exception` that is raised when sending an
improper argument to a function, such as trying to call `List.fill` with a
negative amount.
*/

/**
constructor ValueError(m: String): ValueError
*/
void lily_builtin_ValueError_new(lily_state *s)
{
    return_exception(s, LILY_VALUEERROR_ID);
}

/***
 *      ____                    _                 _
 *     |  _ \ _   _ _ __   __ _| | ___   __ _  __| |
 *     | | | | | | | '_ \ / _` | |/ _ \ / _` |/ _` |
 *     | |_| | |_| | | | | (_| | | (_) | (_| | (_| |
 *     |____/ \__, |_| |_|\__,_|_|\___/ \__,_|\__,_|
 *            |___/
 */

static lily_file_val *new_builtin_file(FILE *source, const char *mode)
{
    lily_file_val *file_val = lily_new_file(source, mode);
    file_val->is_builtin = 1;

    return file_val;
}

static void load_var_stdin(lily_state *s)
{
    lily_push_file(s, new_builtin_file(stdin, "r"));
}

static void load_var_stdout(lily_state *s)
{
    lily_push_file(s, new_builtin_file(stdout, "w"));
}

static void load_var_stderr(lily_state *s)
{
    lily_push_file(s, new_builtin_file(stderr, "w"));
}

extern void lily_builtin__assert(lily_state *);
extern void lily_builtin__calltrace(lily_state *);
extern void lily_builtin__print(lily_state *);

#include "extras_builtin.h"
#include "dyna_builtin.h"

static lily_class *build_class(lily_symtab *symtab, const char *name,
        int generic_count, int dyna_start)
{
    lily_class *result = lily_new_class(symtab, name);
    result->dyna_start = dyna_start;
    result->generic_count = generic_count;
    result->flags |= CLS_IS_BUILTIN;

    return result;
}

/* This handles building classes for which no concrete values will ever exist.
   Giving them a sequential id is a waste because the vm will want to eventually
   scoop it up into the class table. So don't do that. */
static lily_class *build_special(lily_symtab *symtab, const char *name,
        int generic_count, int id)
{
    lily_class *result = lily_new_class(symtab, name);
    result->id = id;
    result->generic_count = generic_count;
    result->flags |= CLS_IS_BUILTIN;

    symtab->active_module->class_chain = result->next;
    symtab->next_class_id--;

    result->next = symtab->old_class_chain;
    symtab->old_class_chain = result;

    return result;
}

void lily_register_pkg_builtin(lily_state *s)
{
    lily_register_package(s, "", lily_builtin_dynaload_table,
            lily_builtin_loader);
}

void lily_init_pkg_builtin(lily_symtab *symtab)
{
    symtab->integer_class    = build_class(symtab, "Integer",     0, INTEGER_OFFSET);
    symtab->double_class     = build_class(symtab, "Double",      0, DOUBLE_OFFSET);
    symtab->string_class     = build_class(symtab, "String",      0, STRING_OFFSET);
    symtab->byte_class       = build_class(symtab, "Byte",        0, BYTE_OFFSET);
    symtab->bytestring_class = build_class(symtab, "ByteString",  0, BYTESTRING_OFFSET);
    symtab->boolean_class    = build_class(symtab, "Boolean",     0, BOOLEAN_OFFSET);
    symtab->function_class   = build_class(symtab, "Function",   -1, FUNCTION_OFFSET);
    symtab->dynamic_class    = build_class(symtab, "Dynamic",     0, DYNAMIC_OFFSET);
    symtab->list_class       = build_class(symtab, "List",        1, LIST_OFFSET);
    symtab->hash_class       = build_class(symtab, "Hash",        2, HASH_OFFSET);
    symtab->tuple_class      = build_class(symtab, "Tuple",      -1, TUPLE_OFFSET);
                               build_class(symtab, "File",        0, FILE_OFFSET);

    symtab->question_class = build_special(symtab, "?", 0, LILY_QUESTION_ID);
    symtab->optarg_class   = build_special(symtab, "*", 1, LILY_OPTARG_ID);
    lily_class *scoop1     = build_special(symtab, "~1", 0, LILY_SCOOP_1_ID);
    lily_class *scoop2     = build_special(symtab, "~2", 0, LILY_SCOOP_2_ID);

    scoop1->self_type->flags |= TYPE_HAS_SCOOP;
    scoop2->self_type->flags |= TYPE_HAS_SCOOP;

    symtab->integer_class->flags    |= CLS_VALID_OPTARG | CLS_VALID_HASH_KEY;
    symtab->double_class->flags     |= CLS_VALID_OPTARG;
    symtab->string_class->flags     |= CLS_VALID_OPTARG | CLS_VALID_HASH_KEY;
    symtab->bytestring_class->flags |= CLS_VALID_OPTARG;
    symtab->boolean_class->flags    |= CLS_VALID_OPTARG;

    /* These need to be set here so type finalization can bubble them up. */
    symtab->question_class->self_type->flags |= TYPE_IS_INCOMPLETE;
    symtab->function_class->flags |= CLS_GC_TAGGED;
    symtab->dynamic_class->flags |= CLS_GC_SPECULATIVE;
    /* HACK: This ensures that there is space to dynaload builtin classes and
       enums into. */
    symtab->next_class_id = START_CLASS_ID;
}
