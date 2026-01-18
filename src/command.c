#define SUBPROCESS_IMPLEMENTATION
#include "../external/subprocess.h"

#define MAX_ARGS 64

typedef struct {
    const char *args[MAX_ARGS];
    int count;
} Cmd;


void cmd_init(Cmd *cmd) {
    cmd->count = 0;
    cmd->args[cmd->count++] = "yt-dlp";
}

void cmd_add(Cmd *cmd, const char *arg) {
    if (cmd->count < MAX_ARGS - 1)
        cmd->args[cmd->count++] = arg;
}

const char **cmd_finalize(Cmd *cmd) {
    cmd->args[cmd->count] = NULL;
    return cmd->args;
}

#include <stdlib.h>
#include <string.h>


char* downloadFromLink(const char* url, const char* file_path) {
    Cmd cmd;
    cmd_init(&cmd);

    cmd_add(&cmd, "-x");
    cmd_add(&cmd, "--audio-format");
    cmd_add(&cmd, "mp3");
    cmd_add(&cmd, "--audio-quality");
    cmd_add(&cmd, "0");
    cmd_add(&cmd, "--embed-metadata");
    cmd_add(&cmd, "--embed-thumbnail");
    cmd_add(&cmd, "--no-progress");

    // This flag ensures the only thing on stdout is the final path
    cmd_add(&cmd, "--print");
    cmd_add(&cmd, "after_move:filepath");

    cmd_add(&cmd, "--paths");
    cmd_add(&cmd, file_path);
    cmd_add(&cmd, "-o");
    cmd_add(&cmd, "%(id)s.%(ext)s");
    cmd_add(&cmd, url);

    const char **argv = cmd_finalize(&cmd);

    struct subprocess_s process;
    int result = subprocess_create(
        argv,
        subprocess_option_search_user_path,
        &process
    );

    if (result != 0) {
        return NULL;
    }

    char buffer[1024];
    char *final_path = NULL;
    unsigned int bytes_read;

    // Read stdout to capture the printed path
    while ((bytes_read = subprocess_read_stdout(&process, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';

        // Remove trailing newline character if present
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        char *carriage = strchr(buffer, '\r');
        if (carriage) *carriage = '\0';

        final_path = strdup(buffer);
    }

    int exit_code;
    subprocess_join(&process, &exit_code);
    subprocess_destroy(&process);

    if (exit_code != 0) {
        free(final_path);
        return NULL;
    }

    return final_path; // Caller must free() this
}
