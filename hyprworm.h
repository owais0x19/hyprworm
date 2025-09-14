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
    char* key;
    char* value;
} WorkspaceAlias;

typedef enum {
    LOG_ERROR = 0,
    LOG_WARNING = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3
} LogLevel;

typedef enum {
    SORT_WORKSPACE = 0,
    SORT_APPLICATION = 1,
    SORT_TITLE = 2,
    SORT_NONE = 3
} SortOrder;

typedef enum {
    SPECIAL_TOP = 0,
    SPECIAL_BOTTOM = 1,
    SPECIAL_DEFAULT = 2
} SpecialPosition;

typedef struct {
    char** launcher_args;
    int launcher_argc;
    int show_title;
    WorkspaceAlias* workspace_aliases;
    int alias_count;
    LogLevel log_level;
    char* log_file;
    SortOrder sort_order;
    SpecialPosition special_position;
} Config;

// Configuration functions
Config* load_config(void);
void free_config(Config* config);
char** parse_launcher_command(const char* command, int* argc);
char* apply_workspace_alias(const char* workspace_name, Config* config);

// Logging functions
void log_message(LogLevel level, const char* format, va_list args);
void log_error(const char* format, ...);
void log_warning(const char* format, ...);
void log_info(const char* format, ...);
void log_debug(const char* format, ...);

// Sorting functions
void sort_window_list(WindowList* list, Config* config);

#endif
