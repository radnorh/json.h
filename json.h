#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>

enum json_type {
    JSON_END,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL,
    JSON_ERROR
};

struct json {
    enum json_type type;
    unsigned length;
    const char *source;
};

size_t json_parse(struct json *json, size_t length, const char *source);
void json_location(const struct json *json, const char *source, size_t *line, size_t *column);
bool json_iterate_object(const struct json **json, const struct json **key, const struct json **value);
bool json_iterate_array(const struct json **json, const struct json **element);
size_t json_unescape(const struct json *json, char *buffer, size_t length);
bool json_strcmp(const struct json *json, const char *string);

#endif

#ifdef JSON_IMPLEMENTATION

#include <limits.h>
#include <string.h>

static bool is_number_part(char c) {
    return ('0' <= c && c <= '9') || c == 'e' || c == 'E' || c == '.' || c == '-' || c == '+';
}

static bool is_delimiter(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ':' || c == ',';
}

static size_t error(struct json *json, size_t length, const char *source) {
    if (length != 0)
        *json = (struct json){JSON_ERROR, 0, source};

    return 0;
}

size_t json_parse(struct json *json, size_t length, const char *source) {
    size_t write_ptr = 0;
    size_t depth = 0;

    if (strncmp(source, "\xEF\xBB\xBF", 3) == 0)
        source += 3;

    for (;;) {
        while (is_delimiter(*source))
            source++;

        if (*source == '\0')
            break;

        struct json result;

        if (*source == '{' || *source == '[') {
            depth++;

            result.type = *source == '{' ? JSON_OBJECT : JSON_ARRAY;
            result.length = 0;
            result.source = source++;
        } else if (*source == '}' || *source == ']') {
            if (depth == 0)
                return error(json, length, source);

            depth--;

            result.type = JSON_END;
            result.length = 0;
            result.source = source++;
        } else if (*source == '"') {
            result.type = JSON_STRING;
            result.source = ++source;

            while (*source != '"') {
                if (*source == '\0')
                    return error(json, length, source);

                if (*source == '\\')
                    source++;

                if (*source != '\0')
                    source++;
            }

            result.length = source++ - result.source;
        } else if (*source == '-' || ('0' <= *source && *source <= '9')) {
            result.type = JSON_NUMBER;
            result.source = source;

            do source++;
            while (is_number_part(*source));

            result.length = source - result.source;
        } else {
            result.source = source;

            if (strncmp(source, "null", 4) == 0) {
                result.type = JSON_NULL;
                result.length = 4;
            } else if (strncmp(source, "true", 4) == 0) {
                result.type = JSON_TRUE;
                result.length = 4;
            } else if (strncmp(source, "false", 5) == 0) {
                result.type = JSON_FALSE;
                result.length = 5;
            } else {
                return error(json, length, source);
            }

            source += result.length;
        }

        if ((size_t)(source - result.source) > UINT_MAX)
            return error(json, length, source);

        if (write_ptr < length)
            json[write_ptr] = result;

        write_ptr++;
    }

    if (depth != 0 || write_ptr == 0)
        return error(json, length, source);

    return write_ptr;
}

void json_location(const struct json *json, const char *source, size_t *line, size_t *column) {
    *line = 1;
    *column = 1;

    for (const char *ptr = source; ptr < json->source; ptr++) {
        if (*ptr == '\n') {
            (*line)++;
            *column = 0;
        }

         (*column)++;
    }
}

bool json_iterate_object(const struct json **json, const struct json **key, const struct json **value) {
    return json_iterate_array(json, key) && json_iterate_array(json, value);
}

static const struct json *reach_end(const struct json *json) {
    size_t depth = 1;
    for (;;) {
        if (json->type == JSON_OBJECT || json->type == JSON_ARRAY)
            depth++;
        else if (json->type == JSON_END && --depth == 0)
            return json;
 
        json++;
    }
}

bool json_iterate_array(const struct json **json, const struct json **element) {
    *element = *json;

    switch ((*json)->type) {
        case JSON_END:
        case JSON_ERROR:
            return false;

        case JSON_OBJECT:
        case JSON_ARRAY:
            *json = reach_end(*json + 1);
            // fall through

        default:
            (*json)++;
            return true;
    }
}

static unsigned long parse_utf16(const struct json *json, size_t *read_ptr) {
    unsigned long codepoint = 0;

    for (int i = 0; i < 4; i++) {
        if (*read_ptr >= json->length)
            return 0xFFFD;

        char hex = json->source[(*read_ptr)++];
        if ('0' <= hex && hex <= '9')
            hex = hex - '0';
        else if ('a' <= hex && hex <= 'f')
            hex = hex - 'a' + 10;
        else if ('A' <= hex && hex <= 'F')
            hex = hex - 'A' + 10;
        else
            return 0xFFFD;

        codepoint |= hex << (12 - i * 4);
    }

    return codepoint;
}

static unsigned long parse_utf32(const struct json *json, size_t *read_ptr) {
    unsigned long codepoint = parse_utf16(json, read_ptr);

    if ((codepoint & 0xF800) != 0xD800)
        return codepoint;

    if ((codepoint & 0xFC00) == 0xD800 && strncmp(json->source + *read_ptr, "\\u", 2) == 0) {
        size_t next_ptr = *read_ptr + 2;
        unsigned long low = parse_utf16(json, &next_ptr);
        if ((low & 0xFC00) == 0xDC00) {
            *read_ptr = next_ptr;
            return 0x10000 + ((codepoint & 0x3FF) << 10) + (low & 0x3FF);
        }
    }

    return 0xFFFD;
}

size_t json_unescape(const struct json *json, char *buffer, size_t length) {
    size_t read_ptr = 0;
    size_t write_ptr = 0;
    bool write_block = false;

    while (read_ptr < json->length) {
        char utf8_bytes[4];
        size_t utf8_length = 1;

        utf8_bytes[0] = json->source[read_ptr++];
        if (utf8_bytes[0] == '\\' && read_ptr < json->length) {
            utf8_bytes[0] = json->source[read_ptr++];

            switch (utf8_bytes[0]) {
                case 'b': utf8_bytes[0] = '\b'; break;
                case 'f': utf8_bytes[0] = '\f'; break;
                case 'n': utf8_bytes[0] = '\n'; break;
                case 'r': utf8_bytes[0] = '\r'; break;
                case 't': utf8_bytes[0] = '\t'; break;

                case 'u': {
                    unsigned long codepoint = parse_utf32(json, &read_ptr);
                    if (codepoint <= 0x7F) {
                        utf8_bytes[0] = codepoint;
                        // utf8_length = 1;
                    } else if (codepoint <= 0x7FF) {
                        utf8_bytes[0] = 0xC0 | (codepoint >> 6);
                        utf8_bytes[1] = 0x80 | (codepoint & 0x3F);
                        utf8_length = 2;
                    } else if (codepoint <= 0xFFFF) {
                        utf8_bytes[0] = 0xE0 | (codepoint >> 12);
                        utf8_bytes[1] = 0x80 | ((codepoint >> 6) & 0x3F);
                        utf8_bytes[2] = 0x80 | (codepoint & 0x3F);
                        utf8_length = 3;
                    } else if (codepoint <= 0x10FFFF) {
                        utf8_bytes[0] = 0xF0 | (codepoint >> 18);
                        utf8_bytes[1] = 0x80 | ((codepoint >> 12) & 0x3F);
                        utf8_bytes[2] = 0x80 | ((codepoint >> 6) & 0x3F);
                        utf8_bytes[3] = 0x80 | (codepoint & 0x3F);
                        utf8_length = 4;
                    }

                    break;
                }
            }
        }

        if (!write_block && write_ptr + utf8_length <= length)
            memcpy(buffer + write_ptr, utf8_bytes, utf8_length);
        else
            write_block = true;

        write_ptr += utf8_length;
    }

    return write_ptr;
}

bool json_strcmp(const struct json *json, const char *string) {
    size_t length = strlen(string);
    return json->length == length && memcmp(json->source, string, length) == 0;
}

#endif
