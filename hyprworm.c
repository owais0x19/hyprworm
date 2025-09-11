#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "hyprworm.h"

#define READ_BUFFER_SIZE 4096
#define MAX_SOCKET_PATH 256

// IPC Communication
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
    if (sock_fd == -1) {
        perror("socket");
        return NULL;
    }

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
            if (!new_response) {
                perror("realloc");
                free(response);
                close(sock_fd);
                return NULL;
            }
            response = new_response;
        }
    }
    response[total_bytes_read] = '\0';
    close(sock_fd);
    return response;
}

//  JSON Parsing 
static char* get_json_string(const cJSON* object, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        return strdup(item->valuestring);
    }
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

    const cJSON* window_json; // cJSON_ArrayForEach uses a const pointer
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
        if (cJSON_IsObject(workspace_obj)) {
            win->workspace_name = get_json_string(workspace_obj, "name");
        } else {
            win->workspace_name = strdup("?");
        }
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

int main() {
    char* json_response = send_hypr_command("j/clients");
    if (!json_response) {
        fprintf(stderr, "Failed to get client list from Hyprland.\n");
        return 1;
    }

    WindowList* windows = parse_window_data(json_response);
    free(json_response);

    if (!windows) {
        fprintf(stderr, "Failed to parse window data.\n");
        return 1;
    }

    printf("--- Parsed Hyprland Windows ---\n");
    for (size_t i = 0; i < windows->count; i++) {
        WindowInfo* win = &windows->windows[i];
        printf("Window %zu: [%s] %s: %s\n", i + 1, win->workspace_name, win->class_name, win->title);
    }

    free_window_list(windows);

    return 0;
}