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
#include "hyprworm.h"

#define READ_BUFFER_SIZE 4096
#define MAX_SOCKET_PATH 256
#define PIPE_READ 0
#define PIPE_WRITE 1

// IPC, Parsing, and Memory Management
char* send_hypr_command(const char* command) {
    const char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char* instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg_runtime_dir || !instance_signature) {
        fprintf(stderr, "Error: Environment variables for Hyprland IPC not set.\n");
        return NULL;
    }
    char socket_path[MAX_SOCKET_PATH];
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket.sock", xdg_runtime_dir, instance_signature);
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) { perror("socket"); return NULL; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock_fd);
        return NULL;
    }
    if (write(sock_fd, command, strlen(command)) == -1) {
        perror("write");
        close(sock_fd);
        return NULL;
    }
    char* response = malloc(READ_BUFFER_SIZE);
    size_t total_bytes_read = 0;
    size_t buffer_size = READ_BUFFER_SIZE;
    ssize_t bytes_read;
    while ((bytes_read = read(sock_fd, response + total_bytes_read, buffer_size - total_bytes_read - 1)) > 0) {
        total_bytes_read += bytes_read;
        if (total_bytes_read >= buffer_size - 1) {
            buffer_size *= 2;
            char* new_response = realloc(response, buffer_size);
            if (!new_response) { perror("realloc"); free(response); close(sock_fd); return NULL; }
            response = new_response;
        }
    }
    response[total_bytes_read] = '\0';
    close(sock_fd);
    return response;
}

static char* get_json_string(const cJSON* object, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && (item->valuestring != NULL)) { return strdup(item->valuestring); }
    return strdup("");
}

WindowList* parse_window_data(const char* json_string) {
    cJSON* root = cJSON_Parse(json_string);
    if (root == NULL || !cJSON_IsArray(root)) {
        fprintf(stderr, "Error: Failed to parse JSON or root is not an array.\n");
        cJSON_Delete(root);
        return NULL;
    }
    WindowList* list = malloc(sizeof(WindowList));
    list->count = 0;
    list->capacity = cJSON_GetArraySize(root);
    list->windows = malloc(list->capacity * sizeof(WindowInfo));
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
        
        if (strcmp(key, "launcher") == 0) {
            launcher_command = strdup(value);
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
    
    free(config);
}

char* launch_frontend(WindowList* list, char** command) { 
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
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "[%s] %s: %s\n", win->workspace_name, win->class_name, win->title);
            write(pipe_to_child[PIPE_WRITE], buffer, strlen(buffer));
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

    // Get window data from Hyprland
    char* json_response = send_hypr_command("j/clients");
    if (!json_response) { 
        free_config(config);
        return 1; 
    }
    WindowList* windows = parse_window_data(json_response);
    free(json_response);
    if (!windows) { 
        free_config(config);
        return 1; 
    }

    // Launch frontend with configured launcher
    char* selection = launch_frontend(windows, config->launcher_args);

    // Focus selected window
    if (selection) {
        char* target_address = NULL;
        for (size_t i = 0; i < windows->count; i++) {
            WindowInfo* win = &windows->windows[i];
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "[%s] %s: %s", win->workspace_name, win->class_name, win->title);
            if (strcmp(selection, buffer) == 0) {
                target_address = win->address;
                break;
            }
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