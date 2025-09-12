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

typedef struct {
    char** launcher_args;
    int launcher_argc;
    int show_title;
    WorkspaceAlias* workspace_aliases;
    int alias_count;
    LogLevel log_level;
    char* log_file;
    int debug_mode;
} Config;

// Configuration functions
Config* load_config(void);
void free_config(Config* config);
char** parse_launcher_command(const char* command, int* argc);
char* apply_workspace_alias(const char* workspace_name, Config* config);

// Logging functions
void log_message(LogLevel level, const char* format, ...);
void log_error(const char* format, ...);
void log_warning(const char* format, ...);
void log_info(const char* format, ...);
void log_debug(const char* format, ...);

#endif