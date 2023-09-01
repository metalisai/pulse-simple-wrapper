#include <stdio.h>

#include "pulse_dotnet.h"
#include <string.h>

#define MAX_STREAMS 16

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

struct pulse_dotnet_stream
{
    pa_stream *stream;
    stream_write_cb_t write_cb;
    void *write_userdata;
    uint32_t active;
    move_stream_cb_t move_cb;
    void *move_userdata;
};

struct pulse_dotnet_input_stream
{
    pa_stream *stream;
    stream_read_cb_t read_cb;
    void *read_userdata;
    int active;
    move_input_stream_cb_t move_cb;
    void *move_userdata;
};

struct pulse_dotnet_ctx
{
    pa_threaded_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;

    context_state_cb_t state_cb;
    void *state_userdata;

    sink_info_cb_t sink_info_cb;
    void *sink_info_userdata;

    source_info_cb_t source_info_cb;
    void *source_info_userdata;

    struct pulse_dotnet_stream streams[MAX_STREAMS];
    struct pulse_dotnet_input_stream input_streams[MAX_STREAMS];
};

static void copy_string(char* dest, const char* src, size_t max_len) {
    strncpy(dest, src, max_len);
    dest[max_len - 1] = '\0';  // Ensure null-termination
}

// This callback gets called when the pa_context_get_sink_info_list() operation completes
static void sink_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    struct pulse_dotnet_ctx *ctx = (struct pulse_dotnet_ctx*)userdata;

    struct pulse_dotnet_sink_info pdn_info;
    if (eol <= 0) {
        copy_string(pdn_info.name, info->name, sizeof(pdn_info.name));
        copy_string(pdn_info.description, info->description, sizeof(pdn_info.description));
        pdn_info.index = info->index;
        pdn_info.sample_rate = info->sample_spec.rate;
        pdn_info.channels = info->sample_spec.channels;
    }
    ctx->sink_info_cb(&pdn_info, eol>0, ctx->sink_info_userdata);
}

static void source_info_cb(pa_context *c, const pa_source_info *info, int eol, void *userdata) {
    struct pulse_dotnet_ctx *ctx = (struct pulse_dotnet_ctx*)userdata;

    struct pulse_dotnet_source_info pdn_info;
    if (eol <= 0) {
        pdn_info.index = info->index;
        copy_string(pdn_info.name, info->name, sizeof(pdn_info.name));
        copy_string(pdn_info.description, info->description, sizeof(pdn_info.description));
        pdn_info.sample_rate = info->sample_spec.rate;
        pdn_info.channels = info->sample_spec.channels;
    }
    ctx->source_info_cb(&pdn_info, eol>0, ctx->source_info_userdata);
}

static void context_state_cb(pa_context *context, void *userdata) {
    struct pulse_dotnet_ctx *ctx = (struct pulse_dotnet_ctx*)userdata;
    ctx->state_cb(pa_context_get_state(context), ctx->state_userdata);
}

static void stream_write_cb(pa_stream *p, size_t nbytes, void *userdata) {
    struct pulse_dotnet_stream *pdn_stream = (struct pulse_dotnet_stream*)userdata; 
    void *mem = malloc(nbytes);
    if (mem != NULL)
    {
        pdn_stream->write_cb(pdn_stream, mem, nbytes, pdn_stream->write_userdata);
        pa_stream_write(pdn_stream->stream, mem, nbytes, free, 0, PA_SEEK_RELATIVE);
    }
}

static void stream_read_cb(pa_stream *s, size_t length, void *userdata) 
{
    struct pulse_dotnet_input_stream *input_stream = (struct pulse_dotnet_input_stream*)userdata;
    const void *data;
    size_t data_length;
    
    pa_stream_peek(s, &data, &data_length);
    // Invoke user read callback
    input_stream->read_cb(input_stream, data, (uint32_t)data_length, input_stream->read_userdata);
    pa_stream_drop(s);
}

static void move_stream_cb(pa_context *c, int success, void *userdata) {
    struct pulse_dotnet_stream *stream = (struct pulse_dotnet_stream*)userdata;
    stream->move_cb((uint32_t)success, stream->move_userdata);
}

static void move_input_stream_cb(pa_context *c, int success, void *userdata)
{
    struct pulse_dotnet_input_stream *stream = (struct pulse_dotnet_input_stream*)userdata;
    stream->move_cb((uint32_t)success, stream->move_userdata);
}

struct pulse_dotnet_ctx* pulse_dotnet_init(const char* name)
{
    struct pulse_dotnet_ctx *ret = calloc(1, sizeof(struct pulse_dotnet_ctx));

    ret->mainloop = pa_threaded_mainloop_new();

    pa_threaded_mainloop_lock(ret->mainloop);
    ret->mainloop_api = pa_threaded_mainloop_get_api(ret->mainloop);
    ret->context = pa_context_new(ret->mainloop_api, name);
    pa_threaded_mainloop_unlock(ret->mainloop);

    return ret;
}

void pulse_dotnet_free(struct pulse_dotnet_ctx *ctx)
{
    pa_threaded_mainloop_stop(ctx->mainloop);
    pa_context_unref(ctx->context);
    pa_threaded_mainloop_free(ctx->mainloop);
    free(ctx);
}

int pulse_dotnet_connect(struct pulse_dotnet_ctx *ctx, context_state_cb_t state_cb, void *userdata)
{
    pa_threaded_mainloop_lock(ctx->mainloop);
    if (pa_context_connect(ctx->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
    {
        fprintf(stderr, "Unable to connect pulseaudio context.\n");
        pa_threaded_mainloop_unlock(ctx->mainloop);
        return -1;
    }
    ctx->state_cb = state_cb;
    pa_context_set_state_callback(ctx->context, context_state_cb, ctx);
    ctx->state_userdata = userdata;

    if (pa_threaded_mainloop_start(ctx->mainloop) < 0)
    {
        fprintf(stderr, "Unable to start pulseaudio mainloop.\n");
        pa_context_disconnect(ctx->context);
        pa_threaded_mainloop_unlock(ctx->mainloop);
        return -1;
    }
    pa_threaded_mainloop_unlock(ctx->mainloop);
    return 0;
}

void pulse_dotnet_begin_get_sink_info_list(struct pulse_dotnet_ctx *ctx, sink_info_cb_t sink_cb, void *userdata)
{
    ctx->sink_info_cb = sink_cb;
    ctx->sink_info_userdata = userdata;

    pa_threaded_mainloop_lock(ctx->mainloop);
    pa_operation *op = pa_context_get_sink_info_list(ctx->context, sink_info_cb, ctx);
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(ctx->mainloop);
}

void pulse_dotnet_begin_get_source_info_list(struct pulse_dotnet_ctx *ctx, source_info_cb_t source_cb, void *userdata)
{
    ctx->source_info_cb = source_cb;
    ctx->source_info_userdata = userdata;

    pa_threaded_mainloop_lock(ctx->mainloop);
    pa_operation *op = pa_context_get_source_info_list(ctx->context, source_info_cb, ctx);
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(ctx->mainloop);
}

struct pulse_dotnet_stream* pulse_dotnet_create_stream(struct pulse_dotnet_ctx *ctx, const char *sink, const char* name, uint32_t sample_rate, uint32_t num_channels, uint32_t target_buffer_size, stream_write_cb_t write_cb, void *userdata)
{
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = sample_rate;
    ss.channels = num_channels;

    struct pulse_dotnet_stream *pdn_stream = NULL;
    for (int i = 0; i < MAX_STREAMS; i++)
    {
        if (ctx->streams[i].active == 0)
        {
            pdn_stream = ctx->streams + i;
            break;
        }
    }
    // max stream count reached
    if (pdn_stream == NULL)
    {
        return NULL;
    }

    pa_buffer_attr buffer_attr;
    buffer_attr.fragsize = (uint32_t)-1;
    buffer_attr.maxlength = (uint32_t)-1;
    buffer_attr.minreq = (uint32_t)-1;
    buffer_attr.tlength = target_buffer_size;
    buffer_attr.prebuf = target_buffer_size;

    pa_threaded_mainloop_lock(ctx->mainloop);
    pa_stream *stream = pa_stream_new(ctx->context, name, &ss, NULL);

    pdn_stream->stream = stream;
    pdn_stream->write_cb = write_cb;
    pdn_stream->write_userdata = userdata;
    pdn_stream->active = 1;

    pa_stream_set_write_callback(stream, stream_write_cb, pdn_stream);
    if (pa_stream_connect_playback(stream, sink, &buffer_attr, 0, NULL, NULL) != 0)
    {
        fprintf(stderr, "Failed to connect stream playback\n");
    }

    pa_threaded_mainloop_unlock(ctx->mainloop);
    return pdn_stream;
}

struct pulse_dotnet_input_stream* pulse_dotnet_create_input_stream(struct pulse_dotnet_ctx *ctx, const char *source, const char *name, uint32_t sample_rate, uint32_t num_channels, uint32_t target_buffer_size, stream_read_cb_t read_cb, void *userdata)
{
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = sample_rate;
    ss.channels = num_channels;

    struct pulse_dotnet_input_stream *pdn_input_stream = NULL;
    for (int i = 0; i < MAX_STREAMS; i++)
    {
        if (ctx->input_streams[i].active == 0)
        {
            pdn_input_stream = ctx->input_streams + i;
            break;
        }
    }
    // max stream count reached
    if (pdn_input_stream == NULL)
    {
        return NULL;
    }

    pa_buffer_attr buffer_attr;
    buffer_attr.fragsize = target_buffer_size;
    buffer_attr.maxlength = (uint32_t)-1;
    buffer_attr.minreq = (uint32_t)-1;
    buffer_attr.tlength = target_buffer_size;
    buffer_attr.prebuf = target_buffer_size;

    pa_threaded_mainloop_lock(ctx->mainloop);
    pa_stream *stream = pa_stream_new(ctx->context, name, &ss, NULL);

    pdn_input_stream->stream = stream;
    pdn_input_stream->read_cb = read_cb;
    pdn_input_stream->read_userdata = userdata;
    pdn_input_stream->active = 1;

    pa_stream_set_read_callback(stream, stream_read_cb, pdn_input_stream);
    if (pa_stream_connect_record(stream, source, &buffer_attr, PA_STREAM_ADJUST_LATENCY) != 0)
    {
        fprintf(stderr, "Failed to connect stream playback\n");
    }

    pa_threaded_mainloop_unlock(ctx->mainloop);
    return pdn_input_stream;
}

void pulse_dotnet_destroy_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_stream *stream)
{
    pa_threaded_mainloop_lock(ctx->mainloop);
    if (stream->active <= 0)
    {
        fprintf(stderr, "Trying to destroy an inactive stream");
        return;
    }

    stream->active = 0;
    pa_stream_disconnect(stream->stream);
    pa_stream_drain(stream->stream, NULL, NULL);
    pa_stream_unref(stream->stream);
    pa_threaded_mainloop_unlock(ctx->mainloop);
}

void pulse_dotnet_destroy_input_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_input_stream *stream)
{
    pa_threaded_mainloop_lock(ctx->mainloop);
    if (stream->active <= 0)
    {
        fprintf(stderr, "Trying to destroy an inactive input stream");
        return;
    }

    stream->active = 0;
    pa_stream_disconnect(stream->stream);
    pa_stream_drain(stream->stream, NULL, NULL);
    pa_stream_unref(stream->stream);
    pa_threaded_mainloop_unlock(ctx->mainloop);
}

void pulse_dotnet_move_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_stream *stream, const char *sink_name, move_stream_cb_t move_cb, void *move_userdata)
{
    pa_threaded_mainloop_lock(ctx->mainloop);
    uint32_t stream_index = pa_stream_get_index(stream->stream);
    stream->move_cb = move_cb;
    stream->move_userdata = move_userdata;
    pa_context_move_sink_input_by_name(ctx->context, stream_index, sink_name, move_stream_cb, stream);
    pa_threaded_mainloop_unlock(ctx->mainloop);
}

void pulse_dotnet_move_input_stream(struct pulse_dotnet_ctx *ctx, struct pulse_dotnet_input_stream *stream, const char *source_name, move_input_stream_cb_t move_cb, void *move_userdata)
{
    pa_threaded_mainloop_lock(ctx->mainloop);
    uint32_t stream_index = pa_stream_get_index(stream->stream);
    stream->move_cb = move_cb;
    stream->move_userdata = move_userdata;
    pa_context_move_source_output_by_name(ctx->context, stream_index, source_name, move_input_stream_cb, stream);
    pa_threaded_mainloop_unlock(ctx->mainloop);
}
