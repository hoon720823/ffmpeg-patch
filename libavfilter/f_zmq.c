/*
 * Copyright (c) 2013 Stefano Sabatini
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * receive commands through libzeromq and broker them to filters
 */

#include "config_components.h"

#include <zmq.h>
#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "avfilter.h"
#include "filters.h"
#include "audio.h"
#include "video.h"
#include "zmq_enc_cmd.h"

/* fms-patches: encoder-command callback storage. See zmq_enc_cmd.h. */
static av_zmq_enc_command_cb fms_zmq_enc_cb = NULL;
static void *fms_zmq_enc_cb_opaque = NULL;
static AVMutex fms_zmq_enc_cb_mu = AV_MUTEX_INITIALIZER;

void av_zmq_set_enc_command_callback(av_zmq_enc_command_cb cb, void *opaque)
{
    ff_mutex_lock(&fms_zmq_enc_cb_mu);
    fms_zmq_enc_cb = cb;
    fms_zmq_enc_cb_opaque = opaque;
    ff_mutex_unlock(&fms_zmq_enc_cb_mu);
}

typedef struct ZMQContext {
    const AVClass *class;
    void *zmq;
    void *responder;
    char *bind_address;
    int command_count;
} ZMQContext;

#define OFFSET(x) offsetof(ZMQContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "bind_address", "set bind address", OFFSET(bind_address), AV_OPT_TYPE_STRING, {.str = "tcp://*:5555"}, 0, 0, FLAGS },
    { "b",            "set bind address", OFFSET(bind_address), AV_OPT_TYPE_STRING, {.str = "tcp://*:5555"}, 0, 0, FLAGS },
    { NULL }
};

static av_cold int init(AVFilterContext *ctx)
{
    ZMQContext *zmq = ctx->priv;

    zmq->zmq = zmq_ctx_new();
    if (!zmq->zmq) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not create ZMQ context: %s\n", zmq_strerror(errno));
        return AVERROR_EXTERNAL;
    }

    zmq->responder = zmq_socket(zmq->zmq, ZMQ_REP);
    if (!zmq->responder) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not create ZMQ socket: %s\n", zmq_strerror(errno));
        return AVERROR_EXTERNAL;
    }

    if (zmq_bind(zmq->responder, zmq->bind_address) == -1) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not bind ZMQ socket to address '%s': %s\n",
               zmq->bind_address, zmq_strerror(errno));
        return AVERROR_EXTERNAL;
    }

    zmq->command_count = -1;
    return 0;
}

static void av_cold uninit(AVFilterContext *ctx)
{
    ZMQContext *zmq = ctx->priv;

    zmq_close(zmq->responder);
    zmq_ctx_destroy(zmq->zmq);
}

typedef struct Command {
    char *target, *command, *arg;
} Command;

#define SPACES " \f\t\n\r"

static int parse_command(Command *cmd, const char *command_str, void *log_ctx)
{
    const char **buf = &command_str;

    cmd->target = av_get_token(buf, SPACES);
    if (!cmd->target || !cmd->target[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "No target specified in command '%s'\n", command_str);
        return AVERROR(EINVAL);
    }

    cmd->command = av_get_token(buf, SPACES);
    if (!cmd->command || !cmd->command[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "No command specified in command '%s'\n", command_str);
        return AVERROR(EINVAL);
    }

    cmd->arg = av_get_token(buf, SPACES);
    return 0;
}

static int recv_msg(AVFilterContext *ctx, char **buf, int *buf_size)
{
    ZMQContext *zmq = ctx->priv;
    zmq_msg_t msg;
    int ret = 0;

    if (zmq_msg_init(&msg) == -1) {
        av_log(ctx, AV_LOG_WARNING,
               "Could not initialize receive message: %s\n", zmq_strerror(errno));
        return AVERROR_EXTERNAL;
    }

    if (zmq_msg_recv(&msg, zmq->responder, ZMQ_DONTWAIT) == -1) {
        if (errno != EAGAIN)
            av_log(ctx, AV_LOG_WARNING,
                   "Could not receive message: %s\n", zmq_strerror(errno));
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    *buf_size = zmq_msg_size(&msg) + 1;
    *buf = av_malloc(*buf_size);
    if (!*buf) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    memcpy(*buf, zmq_msg_data(&msg), *buf_size - 1);
    (*buf)[*buf_size-1] = 0;

end:
    zmq_msg_close(&msg);
    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *ref)
{
    AVFilterContext *ctx = inlink->dst;
    ZMQContext *zmq = ctx->priv;

    while (1) {
        char cmd_buf[1024];
        char *recv_buf, *send_buf;
        int recv_buf_size;
        Command cmd = {0};
        int ret;

        /* receive command */
        if (recv_msg(ctx, &recv_buf, &recv_buf_size) < 0)
            break;
        zmq->command_count++;

        /* parse command */
        if (parse_command(&cmd, recv_buf, ctx) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Could not parse command #%d\n", zmq->command_count);
            goto end;
        }

        /* process command */
        av_log(ctx, AV_LOG_VERBOSE,
               "Processing command #%d target:%s command:%s arg:%s\n",
               zmq->command_count, cmd.target, cmd.command, cmd.arg);

        /* fms-patch: targets prefixed with "@enc_" address an encoder
         * context instead of the filter graph. The callback registered via
         * av_zmq_set_enc_command_callback (typically by fftools/ffmpeg.c)
         * is responsible for mutating the matching AVCodecContext. */
        if (cmd.target && av_strstart(cmd.target, "@enc_", NULL)) {
            av_zmq_enc_command_cb cb;
            void *opaque;

            ff_mutex_lock(&fms_zmq_enc_cb_mu);
            cb = fms_zmq_enc_cb;
            opaque = fms_zmq_enc_cb_opaque;
            ff_mutex_unlock(&fms_zmq_enc_cb_mu);

            if (cb) {
                cb(cmd.target, cmd.command, cmd.arg, opaque);
                av_log(ctx, AV_LOG_INFO,
                       "[fms-patch] enc cmd dispatched: target=%s cmd=%s arg=%s\n",
                       cmd.target, cmd.command, cmd.arg);
                ret = 0;
                send_buf = av_asprintf("0 dispatched");
            } else {
                av_log(ctx, AV_LOG_WARNING,
                       "[fms-patch] enc cmd ignored (no callback registered): %s %s %s\n",
                       cmd.target, cmd.command, cmd.arg);
                ret = AVERROR(ENOSYS);
                send_buf = av_asprintf("%d no enc callback registered", -ret);
            }
            if (!send_buf) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        } else {
            ret = avfilter_graph_send_command(ff_filter_link(inlink)->graph,
                                              cmd.target, cmd.command, cmd.arg,
                                              cmd_buf, sizeof(cmd_buf),
                                              AVFILTER_CMD_FLAG_ONE);
            send_buf = av_asprintf("%d %s%s%s",
                                   -ret, av_err2str(ret), cmd_buf[0] ? "\n" : "", cmd_buf);
            if (!send_buf) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        }
        av_log(ctx, AV_LOG_VERBOSE,
               "Sending command reply for command #%d:\n%s\n",
               zmq->command_count, send_buf);
        if (zmq_send(zmq->responder, send_buf, strlen(send_buf), 0) == -1)
            av_log(ctx, AV_LOG_ERROR, "Failed to send reply for command #%d: %s\n",
                   zmq->command_count, zmq_strerror(ret));

    end:
        av_freep(&send_buf);
        av_freep(&recv_buf);
        recv_buf_size = 0;
        av_freep(&cmd.target);
        av_freep(&cmd.command);
        av_freep(&cmd.arg);
    }

    return ff_filter_frame(ctx->outputs[0], ref);
}

AVFILTER_DEFINE_CLASS_EXT(zmq, "(a)zmq", options);

#if CONFIG_ZMQ_FILTER

static const AVFilterPad zmq_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_zmq = {
    .p.name        = "zmq",
    .p.description = NULL_IF_CONFIG_SMALL("Receive commands through ZMQ and broker them to filters."),
    .p.priv_class  = &zmq_class,
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(ZMQContext),
    FILTER_INPUTS(zmq_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
};

#endif

#if CONFIG_AZMQ_FILTER

static const AVFilterPad azmq_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_af_azmq = {
    .p.name        = "azmq",
    .p.description = NULL_IF_CONFIG_SMALL("Receive commands through ZMQ and broker them to filters."),
    .p.priv_class  = &zmq_class,
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(ZMQContext),
    FILTER_INPUTS(azmq_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
};

#endif
