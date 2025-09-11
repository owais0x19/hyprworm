#ifndef HYPRWORM_H
#define HYPRWORM_H

#include <stddef.h> // For size_t
#include <cjson/cJSON.h>

// Represents a single window's information
typedef struct {
    char* address;
    char* workspace_name;
    char* class_name;
    char* title;
} WindowInfo;

// A dynamic list to hold all open windows
typedef struct {
    WindowInfo* windows;
    size_t count;
    size_t capacity;
} WindowList;

char* send_hypr_command(const char* command);

WindowList* parse_window_data(const char* json_string);

void free_window_list(WindowList* list);

#endif 