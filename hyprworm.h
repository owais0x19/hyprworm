#ifndef HYPRWORM_H
#define HYPRWORM_H

#include <stddef.h>

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

typedef struct {
    char** launcher_args;
    int launcher_argc;
} Config;

// Configuration functions
Config* load_config(void);
void free_config(Config* config);
char** parse_launcher_command(const char* command, int* argc);

#endif