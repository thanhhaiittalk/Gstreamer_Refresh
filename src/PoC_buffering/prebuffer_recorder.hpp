#pragma once

// STL
#include <vector>
#include <deque>
#include <cstdint>
#include <atomic>

// GStreamer
#include <gst/gst.h>        // GstElement, GstBuffer, etc.

struct EncodedFrame {
    std::vector<uint8_t> data; // raw H.264 bytes (Annex-B)
    uint64_t pts;              // nanoseconds
    bool keyframe;             // true if IDR
};

class FrameRing {
public:
    explicit FrameRing(size_t maxFrames) : maxFrames_(maxFrames) {}

    void push(const EncodedFrame& f) {
        if (frames_.size() == maxFrames_) {
            frames_.pop_front(); // drop oldest
        }
        frames_.push_back(f);
    }

    // Return frames starting from last keyframe up to end
    std::vector<EncodedFrame> getPrebufferFromLastKeyframe() const {
        if (frames_.empty()) return {};
        int idx = (int)frames_.size() - 1;

        // Find last keyframe walking backwards
        while (idx >= 0 && !frames_[idx].keyframe) {
            --idx;
        }
        if (idx < 0) idx = 0;

        return std::vector<EncodedFrame>(frames_.begin() + idx, frames_.end());
    }

private:
    size_t maxFrames_;
    std::deque<EncodedFrame> frames_;
};

class PrebufferRecorder {
public:
    PrebufferRecorder()
        : ring_(/*maxFrames*/ 900),  // e.g. 30s * 30fps
          recordingActive_(false),
          pipelineRec_(nullptr),
          appsrcRec_(nullptr) {}

    bool initPipelines();      // create capture + recorder pipelines
    void triggerStart();
    void triggerStop();

    // Called from appsink callback
    void onNewBuffer(GstBuffer* buffer);

private:
    FrameRing ring_;
    std::atomic<bool> recordingActive_;

    GstElement* pipelineCap_;  // v4l2src...appsink
    GstElement* appsinkCap_;

    GstElement* pipelineRec_;  // appsrc...mp4mux
    GstElement* appsrcRec_;
};
