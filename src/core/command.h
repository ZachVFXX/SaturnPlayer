typedef enum {
    CMD_PLAY,
    CMD_PAUSE,
    CMD_STOP,
    CMD_NEXT,
    CMD_PREV,
    CMD_SEEK,
    CMD_SET_VOLUME,
    CMD_QUEUE_ADD,
    CMD_QUEUE_REMOVE,
} CoreCommandType;

typedef struct {
    CoreCommandType type;
    union {
        double seek_seconds;
        float volume;
        const char *path;
        int index;
    };
} CoreCommand;
