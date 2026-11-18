#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define JSON_IMPLEMENTATION
#include "json.h"

static const char example_json[] =
"{\n"
"    \"input\": {\n"
"        \"up\": [\"w\", \"k\", \"up\"],\n"
"        \"down\": [\"s\", \"j\", \"down\"],\n"
"        \"left\": [\"a\", \"h\", \"left\"],\n"
"        \"right\": [\"d\", \"l\", \"right\"]\n"
"    },\n"
"\n"
"    \"window\": {\n"
"        \"position\": {\n"
"            \"x\": 10,\n"
"            \"y\": 20\n"
"        },\n"
"\n"
"        \"size\": {\n"
"            \"width\": 640,\n"
"            \"height\": 480\n"
"        },\n"
"\n"
"        \"title\": \"\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442, \\ud83c\\udf0d!\"\n"
"    },\n"
"\n"
"    \"savepath\": [\n"
"        \".config/project.json\",\n"
"        \".config/project/config.json\"\n"
"    ]\n"
"}";

static void parse_savepath(const struct json *array) {
    if (array->type != JSON_ARRAY)
        return;

    const struct json *iter = array + 1, *element;
    for (size_t index = 1; json_iterate_array(&iter, &element); index++)
        if (element->type == JSON_STRING)
            printf(
                "Savepath #%zu: \"%.*s\"\n",
                index,
                (int)element->length,
                element->source
            );
}

static void parse_keybindings(const struct json *array, const struct json *action) {
    if (array->type != JSON_ARRAY)
        return;

    printf("Keybindings for action \"%.*s\": ", (int)action->length, action->source);

    const struct json *iter = array + 1, *element;
    for (bool comma = false; json_iterate_array(&iter, &element); comma = true) {
        if (comma)
            printf(", ");

        if (element->type == JSON_STRING) 
            printf("\"%.*s\"", (int)element->length, element->source);
    }

    printf("\n");
}

static void parse_input(const struct json *object) {
    if (object->type != JSON_OBJECT)
        return;

    const struct json *iter = object + 1, *key, *value;
    while (json_iterate_object(&iter, &key, &value))
        if (json_strcmp(key, "up") || json_strcmp(key, "down") ||
            json_strcmp(key, "left") || json_strcmp(key, "right"))
            parse_keybindings(value, key);
}

static void parse_window_position(const struct json *object) {
    if (object->type != JSON_OBJECT)
        return;

    const struct json *iter = object + 1, *key, *value;
    while (json_iterate_object(&iter, &key, &value)) {
        if (json_strcmp(key, "x") && value->type == JSON_NUMBER)
            printf("Window position on X-axis: %i\n", atoi(value->source));

        if (json_strcmp(key, "y") && value->type == JSON_NUMBER)
            printf("Window position on Y-axis: %i\n", atoi(value->source));
    }
}

static void parse_window_size(const struct json *object) {
    if (object->type != JSON_OBJECT)
        return;

    const struct json *iter = object + 1, *key, *value;
    while (json_iterate_object(&iter, &key, &value)) {
        if (json_strcmp(key, "width") && value->type == JSON_NUMBER)
            printf("Window width: %i\n", atoi(value->source));

        if (json_strcmp(key, "height") && value->type == JSON_NUMBER)
            printf("Window height: %i\n", atoi(value->source));
    }
}

static void parse_window(const struct json *object) {
    if (object->type != JSON_OBJECT)
        return;

    const struct json *iter = object + 1, *key, *value;
    while (json_iterate_object(&iter, &key, &value)) {
        if (json_strcmp(key, "position"))
            parse_window_position(value);

        if (json_strcmp(key, "size"))
            parse_window_size(value);

        if (json_strcmp(key, "title") && value->type == JSON_STRING) {
            char unescaped[64];
            size_t length = json_unescape(value, unescaped, sizeof(unescaped));

            if (length > sizeof(unescaped)) {
                printf("Window title is too long!\n");
                continue;
            }

            printf("Window title: \"%.*s\"\n", (int)length, unescaped);
        }
    }
}

static void parse_settings(const struct json *object) {
    if (object->type != JSON_OBJECT)
        return;

    const struct json *iter = object + 1, *key, *value;
    while (json_iterate_object(&iter, &key, &value)) {
        if (json_strcmp(key, "input"))
            parse_input(value);

        if (json_strcmp(key, "window"))
            parse_window(value);

        if (json_strcmp(key, "savepath"))
            parse_savepath(value);
    }
}

int main(void) {
    enum { TOKEN_COUNT = 1024 };
    struct json root[TOKEN_COUNT];
    size_t count = json_parse(root, TOKEN_COUNT, example_json);

    if (count == 0) {
        size_t line, column;
        json_location(root, example_json, &line, &column);
        fprintf(
            stderr,
            "E: Syntax error at line %zu, column %zu\n",
            line,
            column
        );
        exit(EXIT_FAILURE);
    }

    if (count > TOKEN_COUNT) {
        fprintf(
            stderr,
            "E: Tokens array is too small (%i) to fit all tokens (%zu)\n",
            TOKEN_COUNT,
            count
        );
        exit(EXIT_FAILURE);
    }

    printf("Used %zu tokens\n", count);

    parse_settings(root);
}
