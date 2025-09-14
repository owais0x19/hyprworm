#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <cjson/cJSON.h>
#include <pwd.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include "hyprworm.h"

#define READ_BUFFER_SIZE 4096
#define MAX_SOCKET_PATH 256
#define PIPE_READ 0
#define PIPE_WRITE 1

static Config* g_config = NULL;

void log_message(LogLevel level, const char* format, va_list args) {
    if (!g_config || level > g_config->log_level) {
        return;
    }
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    
    const char* level_names[] = {"ERROR", "WARNING", "INFO", "DEBUG"};
    
    fprintf(stderr, "[%s] %s: %s\n", timestamp, level_names[level], message);
    
    if (g_config->log_file) {
        FILE* log_fp = fopen(g_config->log_file, "a");
        if (log_fp) {
            fprintf(log_fp, "[%s] %s: %s\n", timestamp, level_names[level], message);
            fclose(log_fp);
        }
    }
    
    va_end(args);
}

void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_ERROR, format, args);
    va_end(args);
}

void log_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_WARNING, format, args);
    va_end(args);
}

void log_info(const char* format, ...){
    va_list args;
    va_start(args, format);
    log_message(LOG_INFO, format, args);
    va_end(args);
}

void log_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_DEBUG, format, args);
    va_end(args);
}

// IPC, Parsing, and Memory Management
char* send_hypr_command(const char* command) {
    log_debug("Sending command to Hyprland: %s", command);
    
    const char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char* instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg_runtime_dir || !instance_signature) {
        log_error("Environment variables for Hyprland IPC not set");
        return NULL;
    }
    char socket_path[MAX_SOCKET_PATH];
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket.sock", xdg_runtime_dir, instance_signature);
    log_debug("Connecting to socket: %s", socket_path);
    
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        log_error("Failed to create socket: %s", strerror(errno));
        return NULL;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        log_error("Failed to connect to Hyprland socket: %s", strerror(errno));
        close(sock_fd);
        return NULL;
    }
    
    if (write(sock_fd, command, strlen(command)) == -1) {
        log_error("Failed to write command to socket: %s", strerror(errno));
        close(sock_fd);
        return NULL;
    }
    char* response = malloc(READ_BUFFER_SIZE);
    if (!response) {
        log_error("Failed to allocate memory for response buffer");
        close(sock_fd);
        return NULL;
    }
    
    size_t total_bytes_read = 0;
    size_t buffer_size = READ_BUFFER_SIZE;
    ssize_t bytes_read;
    
    while ((bytes_read = read(sock_fd, response + total_bytes_read, buffer_size - total_bytes_read - 1)) > 0) {
        total_bytes_read += bytes_read;
        if (total_bytes_read >= buffer_size - 1) {
            buffer_size *= 2;
            char* new_response = realloc(response, buffer_size);
            if (!new_response) {
                log_error("Failed to reallocate response buffer: %s", strerror(errno));
                free(response);
                close(sock_fd);
                return NULL;
            }
            response = new_response;
        }
    }
    
    if (bytes_read == -1) {
        log_error("Failed to read response from socket: %s", strerror(errno));
        free(response);
        close(sock_fd);
        return NULL;
    }
    
    response[total_bytes_read] = '\0';
    log_debug("Received %zu bytes from Hyprland", total_bytes_read);
    close(sock_fd);
    return response;
}

static char* get_json_string(const cJSON* object, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && (item->valuestring != NULL)) { return strdup(item->valuestring); }
    return strdup("");
}

WindowList* parse_window_data(const char* json_string) {
    log_debug("Parsing window data from JSON");
    
    cJSON* root = cJSON_Parse(json_string);
    if (root == NULL) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            log_error("JSON parse error before: %s", error_ptr);
        } else {
            log_error("Failed to parse JSON: unknown error");
        }
        cJSON_Delete(root);
        return NULL;
    }
    
    if (!cJSON_IsArray(root)) {
        log_error("JSON root is not an array");
        cJSON_Delete(root);
        return NULL;
    }
    WindowList* list = malloc(sizeof(WindowList));
    if (!list) {
        log_error("Failed to allocate memory for WindowList");
        cJSON_Delete(root);
        return NULL;
    }
    
    list->count = 0;
    list->capacity = cJSON_GetArraySize(root);
    log_debug("Found %d windows in JSON", list->capacity);
    
    list->windows = malloc(list->capacity * sizeof(WindowInfo));
    if (!list->windows) {
        log_error("Failed to allocate memory for WindowInfo array");
        free(list);
        cJSON_Delete(root);
        return NULL;
    }
    const cJSON* window_json;
    cJSON_ArrayForEach(window_json, root) {
        const cJSON* title_item = cJSON_GetObjectItemCaseSensitive(window_json, "title");
        if (!cJSON_IsString(title_item) || strlen(title_item->valuestring) == 0) {
            continue; 
        }
        WindowInfo* win = &list->windows[list->count];
        win->address = get_json_string(window_json, "address");
        win->class_name = get_json_string(window_json, "class");
        win->title = get_json_string(window_json, "title");
        const cJSON* workspace_obj = cJSON_GetObjectItemCaseSensitive(window_json, "workspace");
        if (cJSON_IsObject(workspace_obj)) { win->workspace_name = get_json_string(workspace_obj, "name"); }
        else { win->workspace_name = strdup("?"); }
        list->count++;
    }
    cJSON_Delete(root);
    return list;
}

void free_window_list(WindowList* list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->windows[i].address);
        free(list->windows[i].workspace_name);
        free(list->windows[i].class_name);
        free(list->windows[i].title);
    }
    free(list->windows);
    free(list);
}

// Configuration parsing functions
char** parse_launcher_command(const char* command, int* argc) {
    if (!command || strlen(command) == 0) {
        *argc = 0;
        return NULL;
    }
    
    int count = 0;
    int in_word = 0;
    for (const char* p = command; *p; p++) {
        if (*p != ' ' && *p != '\t') {
            if (!in_word) {
                count++;
                in_word = 1;
            }
        } else {
            in_word = 0;
        }
    }
    
    if (count == 0) {
        *argc = 0;
        return NULL;
    }
    
    char** args = malloc((count + 1) * sizeof(char*));
    if (!args) {
        perror("malloc");
        *argc = 0;
        return NULL;
    }
    
    int arg_index = 0;
    const char* start = command;
    in_word = 0;
    
    for (const char* p = command; *p; p++) {
        if (*p != ' ' && *p != '\t') {
            if (!in_word) {
                start = p;
                in_word = 1;
            }
        } else {
            if (in_word) {
                size_t len = p - start;
                args[arg_index] = malloc(len + 1);
                if (!args[arg_index]) {
                    perror("malloc");
                    for (int i = 0; i < arg_index; i++) {
                        free(args[i]);
                    }
                    free(args);
                    *argc = 0;
                    return NULL;
                }
                strncpy(args[arg_index], start, len);
                args[arg_index][len] = '\0';
                arg_index++;
                in_word = 0;
            }
        }
    }
    
    if (in_word) {
        size_t len = strlen(start);
        args[arg_index] = malloc(len + 1);
        if (!args[arg_index]) {
            perror("malloc");
            for (int i = 0; i < arg_index; i++) {
                free(args[i]);
            }
            free(args);
            *argc = 0;
            return NULL;
        }
        strcpy(args[arg_index], start);
        arg_index++;
    }
    
    args[arg_index] = NULL;
    *argc = arg_index;
    return args;
}

Config* load_config(void) {
    Config* config = malloc(sizeof(Config));
    if (!config) {
        perror("malloc");
        return NULL;
    }
    
    // Default configuration
    config->launcher_args = NULL;
    config->launcher_argc = 0;
    config->show_title = 0;
    config->workspace_aliases = NULL;
    config->alias_count = 0;
    config->log_level = LOG_INFO;
    config->log_file = NULL;
    config->sort_order = SORT_WORKSPACE;
    config->special_position = SPECIAL_DEFAULT; 
    
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    
    if (!home) {
        fprintf(stderr, "Warning: Could not determine home directory, using default configuration\n");
        char** default_args = parse_launcher_command("fuzzel --dmenu", &config->launcher_argc);
        if (default_args) {
            config->launcher_args = default_args;
        }
        return config;
    }
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/.config/hyprworm/config", home);
    
    FILE* file = fopen(config_path, "r");
    if (!file) {
        if (errno != ENOENT) {
            perror("fopen config");
        }
        char** default_args = parse_launcher_command("fuzzel --dmenu", &config->launcher_argc);
        if (default_args) {
            config->launcher_args = default_args;
        }
        return config;
    }
    
    char line[512];
    char* launcher_command = NULL;
    
    // count workspace aliases
    int alias_count = 0;
    fseek(file, 0, SEEK_SET);
    while (fgets(line, sizeof(line), file)) {
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '#' || *trimmed == '\n' || *trimmed == '\0') continue;
        
        char* equals = strchr(trimmed, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char* key = trimmed;
        while (*key == ' ' || *key == '\t') key++;
        char* key_end = key + strlen(key) - 1;
        while (key_end > key && (*key_end == ' ' || *key_end == '\t' || *key_end == '\n')) {
            *key_end = '\0';
            key_end--;
        }
        
        if (strncmp(key, "workspace_alias_", 16) == 0) {
            alias_count++;
        }
    }
    
    // Allocate space for aliases
    if (alias_count > 0) {
        config->workspace_aliases = malloc(alias_count * sizeof(WorkspaceAlias));
        if (!config->workspace_aliases) {
            perror("malloc");
            fclose(file);
            free_config(config);
            return NULL;
        }
        config->alias_count = 0;
    }
    
    // parse configuration
    fseek(file, 0, SEEK_SET);
    while (fgets(line, sizeof(line), file)) {
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '#' || *trimmed == '\n' || *trimmed == '\0') continue;
        
        char* equals = strchr(trimmed, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char* key = trimmed;
        char* value = equals + 1;
        
        while (*key == ' ' || *key == '\t') key++;
        char* key_end = key + strlen(key) - 1;
        while (key_end > key && (*key_end == ' ' || *key_end == '\t' || *key_end == '\n')) {
            *key_end = '\0';
            key_end--;
        }
        
        while (*value == ' ' || *value == '\t') value++;
        char* value_end = value + strlen(value) - 1;
        while (value_end > value && (*value_end == ' ' || *value_end == '\t' || *value_end == '\n')) {
            *value_end = '\0';
            value_end--;
        }
        
        // remove comments (everything after #)
        char* comment = strchr(value, '#');
        if (comment) {
            *comment = '\0';
            // trim trailing whitespace after removing comment
            value_end = comment - 1;
            while (value_end > value && (*value_end == ' ' || *value_end == '\t')) {
                *value_end = '\0';
                value_end--;
            }
        }
        
        // remove quotes if present
        if (*value == '"' && *value_end == '"' && value_end > value) {
            value++; 
            *value_end = '\0';
        }
        
        if (strcmp(key, "launcher") == 0) {
            launcher_command = strdup(value);
        } else if (strcmp(key, "show_title") == 0) {
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcmp(value, "yes") == 0) {
                config->show_title = 1;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 || strcmp(value, "no") == 0) {
                config->show_title = 0;
            }
        } else if (strncmp(key, "workspace_alias_", 16) == 0) {
            char* alias_name = key + 16;
            if (config->workspace_aliases && config->alias_count < alias_count) {
                config->workspace_aliases[config->alias_count].key = strdup(alias_name);
                config->workspace_aliases[config->alias_count].value = strdup(value);
                config->alias_count++;
            }
        } else if (strcmp(key, "log_level") == 0) {
            if (strcmp(value, "ERROR") == 0) {
                config->log_level = LOG_ERROR;
            } else if (strcmp(value, "WARNING") == 0) {
                config->log_level = LOG_WARNING;
            } else if (strcmp(value, "INFO") == 0) {
                config->log_level = LOG_INFO;
            } else if (strcmp(value, "DEBUG") == 0) {
                config->log_level = LOG_DEBUG;
            }
        } else if (strcmp(key, "log_file") == 0) {
            config->log_file = strdup(value);
        } else if (strcmp(key, "sort_order") == 0) {
            if (strcmp(value, "workspace") == 0) {
                config->sort_order = SORT_WORKSPACE;
            } else if (strcmp(value, "application") == 0) {
                config->sort_order = SORT_APPLICATION;
            } else if (strcmp(value, "title") == 0) {
                config->sort_order = SORT_TITLE;
            } else if (strcmp(value, "none") == 0) {
                config->sort_order = SORT_NONE;
            }
        } else if (strcmp(key, "special_workspace_position") == 0) {
            if (strcmp(value, "top") == 0) {
                config->special_position = SPECIAL_TOP;
            } else if (strcmp(value, "bottom") == 0) {
                config->special_position = SPECIAL_BOTTOM;
            } else if (strcmp(value, "default") == 0) {
                config->special_position = SPECIAL_DEFAULT;
            }
        }
    }
    
    fclose(file);
    
    if (launcher_command) {
        char** args = parse_launcher_command(launcher_command, &config->launcher_argc);
        if (args) {
            config->launcher_args = args;
        }
        free(launcher_command);
    } else {
        char** default_args = parse_launcher_command("fuzzel --dmenu", &config->launcher_argc);
        if (default_args) {
            config->launcher_args = default_args;
        }
    }
    
    return config;
}

void free_config(Config* config) {
    if (!config) return;
    
    if (config->launcher_args) {
        for (int i = 0; i < config->launcher_argc; i++) {
            free(config->launcher_args[i]);
        }
        free(config->launcher_args);
    }
    
    if (config->workspace_aliases) {
        for (int i = 0; i < config->alias_count; i++) {
            free(config->workspace_aliases[i].key);
            free(config->workspace_aliases[i].value);
        }
        free(config->workspace_aliases);
    }
    
    if (config->log_file) {
        free(config->log_file);
    }
    
    free(config);
}

char* apply_workspace_alias(const char* workspace_name, Config* config) {
    if (!workspace_name || !config || !config->workspace_aliases) {
        return strdup(workspace_name);
    }
    
    // First try exact match
    for (int i = 0; i < config->alias_count; i++) {
        if (strcmp(workspace_name, config->workspace_aliases[i].key) == 0) {
            return strdup(config->workspace_aliases[i].value);
        }
    }
    
    // replace the alias key within the workspace name
    char* result = strdup(workspace_name);
    if (!result) return NULL;
    
    for (int i = 0; i < config->alias_count; i++) {
        char* found = strstr(result, config->workspace_aliases[i].key);
        if (found) {
            size_t key_len = strlen(config->workspace_aliases[i].key);
            size_t value_len = strlen(config->workspace_aliases[i].value);
            size_t result_len = strlen(result);
            size_t new_len = result_len - key_len + value_len;
            
            char* new_result = malloc(new_len + 1);
            if (!new_result) {
                free(result);
                return NULL;
            }
            
            size_t before_len = found - result;
            strncpy(new_result, result, before_len);
            new_result[before_len] = '\0';
            
            strcat(new_result, config->workspace_aliases[i].value);
            
            strcat(new_result, found + key_len);
            
            free(result);
            result = new_result;
            
            break;
        }
    }
    
    return result;
}

static int is_special_workspace(const char* workspace_name) {
    if (!workspace_name || workspace_name[0] == '\0') return 0;
    return (workspace_name[0] < '0' || workspace_name[0] > '9');
}

static int get_workspace_sort_value(const char* workspace_name, Config* config) {
    if (is_special_workspace(workspace_name)) {
        switch (config->special_position) {
            case SPECIAL_TOP:
                return -1000;  // Special workspaces at top
            case SPECIAL_BOTTOM:
                return 10000;  // Special workspaces at bottom
            case SPECIAL_DEFAULT:
                // For workspace sorting, default means bottom
                // For other sorting methods, default means mixed in (use 0)
                return (config->sort_order == SORT_WORKSPACE) ? 10000 : 0;
        }
    }
    return atoi(workspace_name);
}

static int compare_workspace(const void* a, const void* b) {
    const WindowInfo* win_a = (const WindowInfo*)a;
    const WindowInfo* win_b = (const WindowInfo*)b;
    
    int val_a = get_workspace_sort_value(win_a->workspace_name, g_config);
    int val_b = get_workspace_sort_value(win_b->workspace_name, g_config);
    
    if (val_a != val_b) return val_a - val_b;
    
    // Secondary sort by application name for same workspace
    return strcmp(win_a->class_name, win_b->class_name);
}

static int compare_application(const void* a, const void* b) {
    const WindowInfo* win_a = (const WindowInfo*)a;
    const WindowInfo* win_b = (const WindowInfo*)b;
    
    // Handle special workspace positioning for non-workspace sorts
    if (g_config->special_position != SPECIAL_DEFAULT) {
        int is_special_a = is_special_workspace(win_a->workspace_name);
        int is_special_b = is_special_workspace(win_b->workspace_name);
        
        if (is_special_a != is_special_b) {
            if (g_config->special_position == SPECIAL_TOP) {
                return is_special_a ? -1 : 1;  // Special workspaces first
            } else {
                return is_special_a ? 1 : -1;  // Special workspaces last
            }
        }
    }
    
    return strcmp(win_a->class_name, win_b->class_name);
}

static int compare_title(const void* a, const void* b) {
    const WindowInfo* win_a = (const WindowInfo*)a;
    const WindowInfo* win_b = (const WindowInfo*)b;
    
    // Handle special workspace positioning for non-workspace sorts
    if (g_config->special_position != SPECIAL_DEFAULT) {
        int is_special_a = is_special_workspace(win_a->workspace_name);
        int is_special_b = is_special_workspace(win_b->workspace_name);
        
        if (is_special_a != is_special_b) {
            if (g_config->special_position == SPECIAL_TOP) {
                return is_special_a ? -1 : 1;  // Special workspaces first
            } else {
                return is_special_a ? 1 : -1;  // Special workspaces last
            }
        }
    }
    
    return strcmp(win_a->title, win_b->title);
}

static int compare_none_with_special(const void* a, const void* b) {
    const WindowInfo* win_a = (const WindowInfo*)a;
    const WindowInfo* win_b = (const WindowInfo*)b;
    
    int is_special_a = is_special_workspace(win_a->workspace_name);
    int is_special_b = is_special_workspace(win_b->workspace_name);
    
    // If one is special and one is not, handle positioning
    if (is_special_a != is_special_b) {
        if (g_config->special_position == SPECIAL_TOP) {
            return is_special_a ? -1 : 1;  // Special workspaces first
        } else if (g_config->special_position == SPECIAL_BOTTOM) {
            return is_special_a ? 1 : -1;  // Special workspaces last
        }
    }
    
    return 0;
}

void sort_window_list(WindowList* list, Config* config) {
    if (!list || !config || list->count <= 1) return;
    
    switch (config->sort_order) {
        case SORT_WORKSPACE:
            qsort(list->windows, list->count, sizeof(WindowInfo), compare_workspace);
            break;
        case SORT_APPLICATION:
            qsort(list->windows, list->count, sizeof(WindowInfo), compare_application);
            break;
        case SORT_TITLE:
            qsort(list->windows, list->count, sizeof(WindowInfo), compare_title);
            break;
        case SORT_NONE:
            if (config->special_position != SPECIAL_DEFAULT) {
                qsort(list->windows, list->count, sizeof(WindowInfo), compare_none_with_special);
            }
            break;
        default:
            // No sorting, keep Hyprland's order
            break;
    }
}

char* launch_frontend(WindowList* list, char** command, int show_title, Config* config) {
    int pipe_to_child[2];   
    int pipe_from_child[2];

    if (pipe(pipe_to_child) == -1 || pipe(pipe_from_child) == -1) {
        perror("pipe");
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return NULL;
    }

    if (pid == 0) {
        dup2(pipe_to_child[PIPE_READ], STDIN_FILENO);
        dup2(pipe_from_child[PIPE_WRITE], STDOUT_FILENO);

        close(pipe_to_child[PIPE_READ]);
        close(pipe_to_child[PIPE_WRITE]);
        close(pipe_from_child[PIPE_READ]);
        close(pipe_from_child[PIPE_WRITE]);

        // Execute the frontend command
        execvp(command[0], command);
        perror("execvp");
        exit(1);
    } else {
        close(pipe_to_child[PIPE_READ]);
        close(pipe_from_child[PIPE_WRITE]);

        for (size_t i = 0; i < list->count; i++) {
            WindowInfo* win = &list->windows[i];
            char* aliased_workspace = apply_workspace_alias(win->workspace_name, config);
            char buffer[1024];
            if (show_title) {
                snprintf(buffer, sizeof(buffer), "[%s] %s: %s\n", aliased_workspace, win->class_name, win->title);
            } else {
                snprintf(buffer, sizeof(buffer), "[%s] %s\n", aliased_workspace, win->class_name);
            }
            write(pipe_to_child[PIPE_WRITE], buffer, strlen(buffer));
            free(aliased_workspace);
        }
        close(pipe_to_child[PIPE_WRITE]);

        char* selection = malloc(512);
        ssize_t bytes_read = read(pipe_from_child[PIPE_READ], selection, 511);

        if (bytes_read > 0) {
            selection[bytes_read] = '\0';
            if (selection[bytes_read - 1] == '\n') {
                selection[bytes_read - 1] = '\0';
            }
        } else {
            free(selection);
            selection = NULL;
        }

        // Clean up the child process
        close(pipe_from_child[PIPE_READ]);
        waitpid(pid, NULL, 0);
        
        return selection;
    }
}

int main() {
    // Load configuration
    Config* config = load_config();
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }
    
    // Set global config for logging
    g_config = config;
    
    log_info("Hyprworm started");
    
    // Get window data from Hyprland
    log_info("Getting window list from Hyprland");
    char* json_response = send_hypr_command("j/clients");
    if (!json_response) { 
        log_error("Failed to get window list from Hyprland");
        free_config(config);
        return 1; 
    }
    
    WindowList* windows = parse_window_data(json_response);
    free(json_response);
    if (!windows) { 
        log_error("Failed to parse window data");
        free_config(config);
        return 1; 
    }
    
    log_info("Found %zu windows", windows->count);
    
    // Sort windows according to configuration
    sort_window_list(windows, config);

    // Launch frontend with configured launcher
    char* selection = launch_frontend(windows, config->launcher_args, config->show_title, config);

    // Focus selected window
    if (selection) {
        char* target_address = NULL;
        for (size_t i = 0; i < windows->count; i++) {
            WindowInfo* win = &windows->windows[i];
            char* aliased_workspace = apply_workspace_alias(win->workspace_name, config);
            char buffer[1024];
            if (config->show_title) {
                snprintf(buffer, sizeof(buffer), "[%s] %s: %s", aliased_workspace, win->class_name, win->title);
            } else {
                snprintf(buffer, sizeof(buffer), "[%s] %s", aliased_workspace, win->class_name);
            }
            if (strcmp(selection, buffer) == 0) {
                target_address = win->address;
                free(aliased_workspace);
                break;
            }
            free(aliased_workspace);
        }

        if (target_address) {
            char command[512];
            snprintf(command, sizeof(command), "dispatch focuswindow address:%s", target_address);
            char* dispatch_response = send_hypr_command(command);
            free(dispatch_response);
        }
        free(selection);
    }

    free_window_list(windows);
    free_config(config);
    return 0;
}
