#include <iostream>
#include <gst/gst.h>
#include "prebuffer_recorder.hpp"

int main(int argc, char* argv[])
{
    // Optional: gst_init here. If you already call it in initPipelines(),
    // multiple gst_init() calls are safe.
    gst_init(&argc, &argv);

    PrebufferRecorder recorder;

    if (!recorder.initPipelines()) {
        std::cerr << "Failed to init pipelines\n";
        return 1;
    }

    std::cout << "Capture pipeline is running (videotestsrc/v4l2src -> appsink).\n";
    std::cout << "Press ENTER to START recording with prebuffer...\n";
    std::cin.get();

    recorder.triggerStart();
    std::cout << "Recording started. Prebuffer + live frames are going to appsrc.\n";
    std::cout << "Press ENTER to STOP recording and finalize MP4 file...\n";
    std::cin.get();

    recorder.triggerStop();
    std::cout << "Recording stopped. File should be written (record.mp4).\n";

    // Optional: full cleanup if you later add methods/destructor.
    // gst_deinit();  // not strictly necessary in a short-lived app

    return 0;
}
