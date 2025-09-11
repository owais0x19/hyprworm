#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "hyprworm.h"
#define READ_BUFFER_SIZE 4096

// Implementation of the IPC function
char* send_hypr_command(const char* command) {
    const char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char* instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    
    if (!xdg_runtime_dir || !instance_signature) {
        fprintf(stderr, "Error: XDG_RUNTIME_DIR or HYPRLAND_INSTANCE_SIGNATURE not set.\n");
        return NULL;
    }
    
    char socket_path[256]; 
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

int main() {
    // Request the client list from Hyprland's IPC
    char* json_response = send_hypr_command("j/clients");
    if (!json_response) {
        fprintf(stderr, "Failed to get client list from Hyprland.\n");
        return 1;
    }
    
    printf("--- Hyprland IPC Response ---\n%s\n", json_response);
    free(json_response);
    return 0;
}