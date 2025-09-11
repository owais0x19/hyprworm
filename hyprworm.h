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

#endif