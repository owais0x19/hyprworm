#ifndef HYPRWORM_H
#define HYPRWORM_H

#include <stddef.h> // For size_t
#include <cjson/cJSON.h>

typedef struct {
    char* address;
    char* workspace_name;
    char* class_name;
    char* title;
} WindowInfo;

typedef struct {
    WindowInfo* windows;
    size_t count;
    size_t capacity;
} WindowList;

char* send_hypr_command(const char* command);

WindowList* parse_window_data(const char* json_string);

// Frees all memory associated with a WindowList.
void free_window_list(WindowList* list);

// Helper function to duplicate strings
char* my_strdup(const char* str);

#endif