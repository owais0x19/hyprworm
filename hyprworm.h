#ifndef HYPRWORM_H
#define HYPRWORM_H

// Sends a command to the Hyprland IPC socket and returns the response.
char* send_hypr_command(const char* command);

#endif