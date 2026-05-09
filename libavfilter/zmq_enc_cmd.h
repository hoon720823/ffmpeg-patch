/*
 * fms-patches: ZMQ encoder-command callback (Phase 2 of abr-zmq-patches)
 *
 * The mainline ZMQ filter (f_zmq.c) routes commands through
 * avfilter_graph_send_command() which only reaches filter graph nodes. fms
 * needs to mutate the encoder's AVCodecContext (bit_rate / rc_max_rate /
 * rc_buffer_size) mid-stream so libavcodec's per-encoder reconfig_encoder()
 * can pick up the change on the next SendFrame.
 *
 * This header exposes a single global callback that fftools/ffmpeg.c
 * registers at startup. When the ZMQ filter sees a target prefixed with
 * "@enc_" it forwards (target, command, arg) to the callback. The callback
 * runs on the filter graph thread — implementations must take whatever
 * synchronization the encoder's AVCodecContext requires.
 *
 * Out-of-band design intent (kept short on purpose):
 *   - Single global callback. One ffmpeg process = one main loop = one
 *     mapping table. No per-stream callback registration.
 *   - target format = "@enc_<output_stream_idx>" matching ffmpeg's
 *     -map output ordering. Parsing of <output_stream_idx> is the
 *     callback's responsibility.
 *   - command/arg are pass-through. Typical commands: "b", "maxrate",
 *     "minrate", "bufsize". The callback decides which fields each maps to.
 *   - Returning from the callback acknowledges the command; the ZMQ
 *     filter sends a generic "0 dispatched" back to the requester.
 *     Per-command success/failure plumbing is a future extension.
 *
 * This API is not available when ZMQ is disabled at configure time. Callers
 * should guard registration with `#if CONFIG_ZMQ_FILTER || CONFIG_AZMQ_FILTER`.
 */

#ifndef AVFILTER_ZMQ_ENC_CMD_H
#define AVFILTER_ZMQ_ENC_CMD_H

typedef void (*av_zmq_enc_command_cb)(const char *target,
                                      const char *command,
                                      const char *arg,
                                      void *opaque);

/**
 * Register (or clear with cb=NULL) the callback that the ZMQ filter calls
 * when it receives a command targeted at "@enc_*". Thread-safe.
 *
 * The callback runs on the filter graph thread that processes the ZMQ
 * filter. fftools/ffmpeg.c registers a callback that mutates the matching
 * output stream's encoder context.
 */
void av_zmq_set_enc_command_callback(av_zmq_enc_command_cb cb, void *opaque);

#endif /* AVFILTER_ZMQ_ENC_CMD_H */
