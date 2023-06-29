#include <pulse/pulseaudio.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

enum pulse_dotnet_state_e
{
    PA_DOTNET_CTX_UNCONNECTED,
    PA_DOTNET_CTX_CONNECTING,
    PA_DOTNET_CTX_AUTHORIZING,
    PA_DOTNET_CTX_SETTING_NAME,
    PA_DOTNET_CTX_READY,
    PA_DOTNET_CTX_FAILED,
    PA_DOTNET_CTX_TERMINATED,
};

struct pulse_dotnet_sink_info
{
    uint32_t index;
    char name[128];
    char description[256];
    uint32_t sample_rate;
    uint32_t channels;
};

struct pulse_dotnet_source_info {
    uint32_t index;
    char name[128];
    char description[256];
    uint32_t sample_rate;
    uint32_t channels;
};

struct pulse_dotnet_ctx;
struct pulse_dotnet_stream;
struct pulse_dotnet_input_stream;

typedef void (*sink_info_cb_t)(struct pulse_dotnet_sink_info *info, uint32_t eol, void *userdata);
typedef void (*source_info_cb_t)(struct pulse_dotnet_source_info *info, uint32_t eol, void *userdata);
typedef void (*stream_write_cb_t)(struct pulse_dotnet_stream *stream, uint8_t *buf, uint32_t nbytes, void *userdata);
typedef void (*stream_read_cb_t)(struct pulse_dotnet_input_stream *in_stream, const uint8_t *data, uint32_t length, void *userdata);
typedef void (*context_state_cb_t)(enum pulse_dotnet_state_e state, void *userdata);
typedef void (*move_stream_cb_t)(uint32_t success, void *userdata);
typedef void (*move_input_stream_cb_t)(uint32_t success, void *userdata);

// create context, start loop
EXPORT struct pulse_dotnet_ctx* pulse_dotnet_init(const char *name);
EXPORT int pulse_dotnet_connect(struct pulse_dotnet_ctx *ctx, context_state_cb_t state_cb, void *userdata);
// stop and free everything
EXPORT void pulse_dotnet_free(struct pulse_dotnet_ctx *ctx);
// initiate sink info listing
EXPORT void pulse_dotnet_begin_get_sink_info_list(struct pulse_dotnet_ctx *ctx, sink_info_cb_t sink_cb, void *userdata);
EXPORT void pulse_dotnet_begin_get_source_info_list(struct pulse_dotnet_ctx *ctx, source_info_cb_t source_cb, void *userdata);
EXPORT struct pulse_dotnet_stream* pulse_dotnet_create_stream(struct pulse_dotnet_ctx *ctx, const char* sink, const char* name, uint32_t sample_rate, uint32_t num_channels, uint32_t target_buffer_size, stream_write_cb_t stream_write_cb, void *userdata);
EXPORT struct pulse_dotnet_input_stream* pulse_dotnet_create_input_stream(struct pulse_dotnet_ctx *ctx, const char *source, const char *name, uint32_t sample_rate, uint32_t num_channels, uint32_t target_buffer_size, stream_read_cb_t read_cb, void *userdata);
EXPORT void pulse_dotnet_destroy_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_stream *stream);
EXPORT void pulse_dotnet_destroy_input_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_input_stream *stream);
EXPORT void pulse_dotnet_move_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_stream *stream, const char *sink_name, move_stream_cb_t move_cb, void *move_userdata);

EXPORT void pulse_dotnet_move_input_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_input_stream *stream, const char *source_name, move_input_stream_cb_t move_cb, void *move_userdata);
