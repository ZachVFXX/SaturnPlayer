#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "array.h"



typedef struct {
    char* items;
    int64_t count;
    int64_t capacity;
} StringBuilder;

char* cstr_dup(const char *str) {
    int64_t len = strlen(str) + 1;
    char *copy = malloc(len);
    if (!copy) return NULL;
    memcpy(copy, str, len);
    return copy;
}

StringBuilder sb_from_cstr(char* cstr) {
    StringBuilder sb = {0};
    int64_t len = strlen(cstr);
    sb.items = cstr_dup(cstr);
    sb.count = len;
    sb.capacity = len + 1; // Include null terminator in capacity
    return sb;
}

void sb_free(StringBuilder* sb) {
    if (sb->items != NULL) {
        free(sb->items);
    }
    sb->count = 0;
    sb->capacity = 0;
}

bool sb_equal(StringBuilder sb1, StringBuilder sb2) {
    if (sb1.count != sb2.count) return false;
    if (memcmp(sb1.items, sb2.items, sb1.count) == 0) return true;
    return false;
}

#define sb_append_buf(sb, buf, size) da_append_many(sb, buf, size)


typedef struct {
    int64_t count;
    const char *data;
} StringView;

// StringView function declarations
StringView sv_chop_by_delim(StringView *sv, char delim);
StringView sv_chop_left(StringView *sv, int64_t n);
StringView sv_trim(StringView sv);
StringView sv_trim_left(StringView sv);
StringView sv_trim_right(StringView sv);
bool sv_equal(StringView a, StringView b);
bool sv_end_with(StringView sv, const char *cstr);
bool sv_starts_with(StringView sv, StringView expected_prefix);
StringView sv_from_cstr(const char *cstr);
StringView sv_from_parts(const char *data, int64_t count);
int64_t sv_find(StringView sv, StringView needle);
bool sv_contains(StringView sv, StringView needle);

// Dynamic array of StringViews for split operations
typedef struct {
    StringView* items;
    int64_t count;
    int64_t capacity;
} StringViewArray;

StringViewArray sv_split_by_delim(StringView sv, char delim);
void sv_array_free(StringViewArray* arr);

// StringBuilder function declarations
void sb_append_cstr(StringBuilder* sb, const char* cstr);
void sb_append_char(StringBuilder* sb, char c);
void sb_append_sv(StringBuilder* sb, StringView sv);
void sb_append_null(StringBuilder* sb);
char* sb_to_cstr(StringBuilder* sb);
StringBuilder sb_from_sv(StringView sv);
void sb_replace_first(StringBuilder* sb, const char* target, const char* replacement);
void sb_replace_all(StringBuilder* sb, const char* target, const char* replacement);
void sb_to_upper(StringBuilder* sb);
void sb_to_lower(StringBuilder* sb);

// sb_to_sv() enables you to just view String_Builder as StringView
#define sb_to_sv(sb) sv_from_parts((sb).items, (sb).count)


StringView sv_chop_by_delim(StringView *sv, char delim) {
    int64_t i = 0;
    while (i < sv->count && sv->data[i] != delim) {
        i += 1;
    }

    StringView result = sv_from_parts(sv->data, i);

    if (i < sv->count) {
        sv->count -= i + 1;
        sv->data  += i + 1;
    } else {
        sv->count -= i;
        sv->data  += i;
    }

    return result;
}

StringView sv_chop_left(StringView *sv, int64_t n)
{
    if (n > sv->count) {
        n = sv->count;
    }

    StringView result = sv_from_parts(sv->data, n);

    sv->data  += n;
    sv->count -= n;

    return result;
}

StringView sv_from_parts(const char *data, int64_t count)
{
    StringView sv;
    sv.count = count;
    sv.data = data;
    return sv;
}

StringView sv_trim_left(StringView sv)
{
    int64_t i = 0;
    while (i < sv.count && isspace(sv.data[i])) {
        i += 1;
    }
    return sv_from_parts(sv.data + i, sv.count - i);
}

StringView sv_trim_right(StringView sv)
{
    int64_t i = 0;
    while (i < sv.count && isspace(sv.data[sv.count - 1 - i])) {
        i += 1;
    }

    return sv_from_parts(sv.data, sv.count - i);
}

StringView sv_trim(StringView sv)
{
    return sv_trim_right(sv_trim_left(sv));
}

StringView sv_from_cstr(const char *cstr)
{
    return sv_from_parts(cstr, strlen(cstr));
}

bool sv_equal(StringView a, StringView b)
{
    if (a.count != b.count) {
        return false;
    } else {
        return memcmp(a.data, b.data, a.count) == 0;
    }
}

bool sv_end_with(StringView sv, const char *cstr)
{
    int64_t cstr_count = strlen(cstr);
    if (sv.count >= cstr_count) {
        int64_t ending_start = sv.count - cstr_count;
        StringView sv_ending = sv_from_parts(sv.data + ending_start, cstr_count);
        return sv_equal(sv_ending, sv_from_cstr(cstr));
    }
    return false;
}


bool sv_starts_with(StringView sv, StringView expected_prefix)
{
    if (expected_prefix.count <= sv.count) {
        StringView actual_prefix = sv_from_parts(sv.data, expected_prefix.count);
        return sv_equal(expected_prefix, actual_prefix);
    }

    return false;
}

// Find the first occurrence of needle in sv, returns index or -1 if not found
int64_t sv_find(StringView sv, StringView needle)
{
    if (needle.count == 0) return 0;
    if (needle.count > sv.count) return -1;

    for (int64_t i = 0; i <= sv.count - needle.count; i++) {
        StringView candidate = sv_from_parts(sv.data + i, needle.count);
        if (sv_equal(candidate, needle)) {
            return i;
        }
    }
    return -1;
}

// Check if sv contains needle
bool sv_contains(StringView sv, StringView needle)
{
    return sv_find(sv, needle) != -1;
}

// Append a C string to StringBuilder
void sb_append_cstr(StringBuilder* sb, const char* cstr)
{
    int64_t len = strlen(cstr);
    sb_append_buf(sb, cstr, len);
}

// Append a single character to StringBuilder
void sb_append_char(StringBuilder* sb, char c)
{
    da_append(sb, c);
}

// Append a StringView to StringBuilder
void sb_append_sv(StringBuilder* sb, StringView sv)
{
    sb_append_buf(sb, sv.data, sv.count);
}

// Append a null terminator to StringBuilder (for C string compatibility)
void sb_append_null(StringBuilder* sb)
{
    char null_char = '\0';
    da_append(sb, null_char);
    sb->count--; // Don't count the null terminator in the string length
}

// Convert StringBuilder to a null-terminated C string (creates a new copy)
// The caller is responsible for freeing the returned string
char* sb_to_cstr(StringBuilder* sb)
{
    char* result = malloc(sb->count + 1);
    if (!result) return NULL;
    memcpy(result, sb->items, sb->count);
    result[sb->count] = '\0';
    return result;
}

// Create a StringBuilder from a StringView
StringBuilder sb_from_sv(StringView sv)
{
    StringBuilder sb = {0};
    sb_append_buf(&sb, sv.data, sv.count);
    return sb;
}

// Replace the first occurrence of target with replacement in StringBuilder
void sb_replace_first(StringBuilder* sb, const char* target, const char* replacement)
{
    StringView sv_target = sv_from_cstr(target);
    StringView sv_current = sb_to_sv(*sb);

    int64_t pos = sv_find(sv_current, sv_target);
    if (pos == -1) return; // Target not found

    int64_t target_len = sv_target.count;
    int64_t replacement_len = strlen(replacement);

    // Create new StringBuilder with the replacement
    StringBuilder new_sb = {0};

    // Append everything before the target
    sb_append_buf(&new_sb, sb->items, pos);

    // Append the replacement
    sb_append_buf(&new_sb, replacement, replacement_len);

    // Append everything after the target
    sb_append_buf(&new_sb, sb->items + pos + target_len, sb->count - pos - target_len);

    // Replace the old StringBuilder with the new one
    sb_free(sb);
    *sb = new_sb;
}

// Replace all occurrences of target with replacement in StringBuilder
void sb_replace_all(StringBuilder* sb, const char* target, const char* replacement)
{
    StringView sv_target = sv_from_cstr(target);
    int64_t target_len = sv_target.count;
    int64_t replacement_len = strlen(replacement);

    if (target_len == 0) return; // Can't replace empty string

    StringBuilder new_sb = {0};
    int64_t last_pos = 0;

    StringView sv_current = sb_to_sv(*sb);

    while (true) {
        // Create a view from the last position
        StringView remaining = sv_from_parts(sv_current.data + last_pos, sv_current.count - last_pos);
        int64_t pos = sv_find(remaining, sv_target);

        if (pos == -1) {
            // No more occurrences, append the rest
            sb_append_buf(&new_sb, sv_current.data + last_pos, sv_current.count - last_pos);
            break;
        }

        // Append everything before this occurrence
        sb_append_buf(&new_sb, sv_current.data + last_pos, pos);

        // Append the replacement
        sb_append_buf(&new_sb, replacement, replacement_len);

        // Move past this occurrence
        last_pos += pos + target_len;
    }

    // Replace the old StringBuilder with the new one
    sb_free(sb);
    *sb = new_sb;
}

// Convert StringBuilder to uppercase
void sb_to_upper(StringBuilder* sb)
{
    for (int64_t i = 0; i < sb->count; i++) {
        sb->items[i] = toupper((unsigned char)sb->items[i]);
    }
}

// Convert StringBuilder to lowercase
void sb_to_lower(StringBuilder* sb)
{
    for (int64_t i = 0; i < sb->count; i++) {
        sb->items[i] = tolower((unsigned char)sb->items[i]);
    }
}

// Split a StringView by delimiter into an array of StringViews
StringViewArray sv_split_by_delim(StringView sv, char delim)
{
    StringViewArray result = {0};

    while (sv.count > 0) {
        StringView part = sv_chop_by_delim(&sv, delim);
        da_append(&result, part);
    }

    return result;
}

// Free a StringViewArray (note: doesn't free the underlying string data)
void sv_array_free(StringViewArray* arr)
{
    if (arr->items != NULL) {
        free(arr->items);
        arr->items = NULL;
    }
    arr->count = 0;
    arr->capacity = 0;
}
