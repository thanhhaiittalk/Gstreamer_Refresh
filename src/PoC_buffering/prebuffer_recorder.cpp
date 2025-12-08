// ===== Your own header =====
#include "prebuffer_recorder.hpp"

// ===== GStreamer Core =====
#include <gst/gst.h>

// ===== GStreamer App (appsrc + appsink) =====
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

// ===== Glib (signals, gboolean, etc.) =====
#include <glib.h>

// ===== C++ STL =====
#include <cstring>     // for memcpy if needed later
#include <iostream>   // optional: for logging/debug

static GstFlowReturn on_new_sample(GstAppSink* sink, gpointer user_data) {
    auto* self = static_cast<PrebufferRecorder*>(user_data);

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_OK;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    self->onNewBuffer(buffer);

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

void PrebufferRecorder::onNewBuffer(GstBuffer* buffer) {
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        return;

    // Detect keyframe: DELTA_UNIT not set => keyframe
    bool key = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    uint64_t pts = GST_BUFFER_PTS_IS_VALID(buffer) ? GST_BUFFER_PTS(buffer) : 0;

    // Copy into RAM
    EncodedFrame f;
    f.data.assign(map.data, map.data + map.size);
    f.pts = pts;
    f.keyframe = key;
    ring_.push(f);

    totalFrames_++;

    // If recording, also push to appsrc
    if (recordingActive_ && appsrcRec_) {
        GstBuffer* out = gst_buffer_new_allocate(nullptr, map.size, nullptr);
        gst_buffer_fill(out, 0, map.data, map.size);
        GST_BUFFER_PTS(out) = pts;
        GST_BUFFER_DTS(out) = pts;
        gst_app_src_push_buffer(GST_APP_SRC(appsrcRec_), out);
        livePushed_++;
    }

    gst_buffer_unmap(buffer, &map);
}

bool PrebufferRecorder::initPipelines() {
    gst_init(nullptr, nullptr);

    // Capture pipeline
    const char* capDesc =
            "videotestsrc is-live=true pattern=ball ! "
            "video/x-raw,framerate=30/1,width=640,height=360 ! "
            "videoconvert ! "
            "x264enc tune=zerolatency key-int-max=30 bitrate=2000 speed-preset=ultrafast ! "
            "h264parse config-interval=-1 ! "
            "video/x-h264,stream-format=byte-stream,alignment=au ! "
            "appsink name=cap sync=false emit-signals=true";;

    pipelineCap_ = gst_parse_launch(capDesc, nullptr);
    appsinkCap_  = gst_bin_get_by_name(GST_BIN(pipelineCap_), "cap");
    gst_app_sink_set_emit_signals(GST_APP_SINK(appsinkCap_), TRUE);
    g_signal_connect(appsinkCap_, "new-sample", G_CALLBACK(on_new_sample), this);
    gst_element_set_state(pipelineCap_, GST_STATE_PLAYING);

    // Recorder pipeline (prewarmed)
    const char* recDesc =
        "appsrc name=rec is-live=false format=time "
        "caps=video/x-h264,stream-format=byte-stream,alignment=au "
        "! h264parse ! mp4mux ! filesink location=record.mp4";

    pipelineRec_ = gst_parse_launch(recDesc, nullptr);
    appsrcRec_   = gst_bin_get_by_name(GST_BIN(pipelineRec_), "rec");
    gst_element_set_state(pipelineRec_, GST_STATE_PLAYING);

    return true;
}

void PrebufferRecorder::triggerStart() {
    if (recordingActive_) return;

    // 1) Push preroll from RAM (from last keyframe)
    auto pre = ring_.getPrebufferFromLastKeyframe();
    for (const auto& f : pre) {
        GstBuffer* buf = gst_buffer_new_allocate(nullptr, f.data.size(), nullptr);
        gst_buffer_fill(buf, 0, f.data.data(), f.data.size());
        GST_BUFFER_PTS(buf) = f.pts;
        GST_BUFFER_DTS(buf) = f.pts;
        gst_app_src_push_buffer(GST_APP_SRC(appsrcRec_), buf);
        prebufferPushed_++;
    }

    // 2) Now start also pushing live frames in onNewBuffer()
    recordingActive_ = true;
}

void PrebufferRecorder::triggerStop() {
    if (!recordingActive_) return;
    recordingActive_ = false;

    std::cout << "Stats:\n";
    std::cout << "  totalFrames_        = " << totalFrames_ << "\n";
    std::cout << "  prebufferPushed_    = " << prebufferPushed_ << "\n";
    std::cout << "  livePushed_         = " << livePushed_ << "\n";

    // Tell appsrc no more buffers
    gst_app_src_end_of_stream(GST_APP_SRC(appsrcRec_));

    // (For PoC you can block here and wait for EOS on pipelineRec_ bus)
    // In production: run a bus watch and react to EOS.

    // Example (simplified):
    GstBus* bus = gst_element_get_bus(pipelineRec_);
    GstMessage* msg = gst_bus_timed_pop_filtered(
        bus, GST_CLOCK_TIME_NONE,
        (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (msg) gst_message_unref(msg);
    gst_object_unref(bus);

    // Stop recorder pipeline if you want to close file
    gst_element_set_state(pipelineRec_, GST_STATE_NULL);
}
