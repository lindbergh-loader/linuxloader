
#define JVSBUFFER_SIZE 256

typedef struct
{
    int ctsCounter;
    int ready;
    int size;
    char buffer[JVSBUFFER_SIZE];
} JVSFrame;

typedef enum
{
    JVS_PASSTHROUGH_STATUS_OK = 0,
    JVS_PASSTHROUGH_STATUS_ERROR = 1,
    JVS_PASSTHROUGH_STATUS_TIMEOUT = 2
} JVSPassthroughStatus;

JVSPassthroughStatus initJVSPassthrough(char *jvsPath);
JVSPassthroughStatus writeJVSFrame(unsigned char *buffer, int size);
JVSPassthroughStatus readJVSFrame(unsigned char *buffer, int *size);