#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <cjson/cJSON.h>
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

// UI Bridge
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
    // Get window data from Hyprland
    char* json_response = send_hypr_command("j/clients");
    if (!json_response) { return 1; }
    WindowList* windows = parse_window_data(json_response);
    free(json_response);
    if (!windows) { return 1; }

    char* frontend_cmd[] = {"fuzzel", "--dmenu", NULL};
    char* selection = launch_frontend(windows, frontend_cmd);

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
    return 0;
}