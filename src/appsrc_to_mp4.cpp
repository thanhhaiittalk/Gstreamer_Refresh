// src/appsrc_to_mp4.cpp -- top of file
#include <gst/gst.h>            // include core first
#include <gst/gstbuffer.h>     // gst_buffer_set_pts / set_duration etc.
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>   // optional, useful

#include <iostream>
#include <vector>
#include <cstring>

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    // Pipeline string (matches what you gave)
    const char *pipeline_desc =
    "appsrc name=src is-live=false format=time "
    "caps=video/x-raw,format=RGB,width=320,height=240,framerate=30/1 "
    "! videoconvert "
    "! x264enc tune=zerolatency key-int-max=30 "
    "! mp4mux "
    "! filesink location=out.mp4";

    
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_desc, &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << (error ? error->message : "unknown") << "\n";
        g_clear_error(&error);
        return 1;
    }

        // Find appsrc by name (as requested)
    GstElement *appsrc_elem = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    if (!appsrc_elem) {
        std::cerr << "Failed to find appsrc named 'src'\n";
        gst_object_unref(pipeline);
        return 1;
    }
    GstAppSrc *appsrc = GST_APP_SRC(appsrc_elem);


    // Parameters for fake stream
    const guint fps = 30;
    const GstClockTime frame_duration = GST_SECOND / fps; // duration per frame
    const guint n_frames = 150; // e.g., 5 seconds @ 30fps
    const guint payload_size = 1024; // bytes of dummy payload (after start code)
    const guint start_code_size = 4; // 0x00 00 00 01
    const guint buffer_size = start_code_size + payload_size;

    // set pipeline to PLAYING so downstream elements process the buffers
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to set pipeline to PLAYING\n";
        gst_object_unref(appsrc_elem);
        gst_object_unref(pipeline);
        return 1;
    }

    GstFlowReturn ret;
    for (guint i = 0; i < n_frames; ++i) {
        GstBuffer *buf = gst_buffer_new_allocate(NULL, buffer_size, NULL);
        if (!buf) {
            std::cerr << "Failed to alloc GstBuffer\n";
            break;
        }

        // Map and fill
        GstMapInfo map;
        if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
            // start code
            map.data[0] = 0x00;
            map.data[1] = 0x00;
            map.data[2] = 0x00;
            map.data[3] = 0x01;

            // payload: for clarity set a simple pattern (not random)
            // For a keyframe we put payload bytes = 0x65 (IDR NAL type if real)
            // For non-key, use 0x41 (non-IDR). This doesn't magically create a decoder-friendly stream,
            // but it helps tags if a parser inspects the first payload byte.
            guint8 fill_byte = (i % 30 == 0) ? 0x65 : 0x41;
            for (guint j = start_code_size; j < buffer_size; ++j) {
                map.data[j] = fill_byte;
            }
            gst_buffer_unmap(buf, &map);
        } else {
            std::cerr << "Failed to map buffer\n";
            gst_buffer_unref(buf);
            break;
        }

        // Set timestamps
        GstClockTime pts = (GstClockTime)(i) * frame_duration;
        GST_BUFFER_PTS(buf) = pts;
        GST_BUFFER_DTS(buf) = pts;
        GST_BUFFER_DURATION(buf) = frame_duration;

        // Push buffer into appsrc
        ret = gst_app_src_push_buffer(appsrc, buf);
        if (ret != GST_FLOW_OK) {
            std::cerr << "gst_app_src_push_buffer returned flow " << ret << "\n";
            // buffer ownership transferred to appsrc on success; on error some implementations still take it,
            // but to be safe we simply break.
            break;
        }
    }

    // End-of-stream
    gst_app_src_end_of_stream(appsrc);

    // Wait for EOS or ERROR on bus
    GstBus *bus = gst_element_get_bus(pipeline);
    bool done = false;
    while (!done) {
        GstMessage *msg = gst_bus_timed_pop_filtered(bus,
            GST_CLOCK_TIME_NONE,
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

        if (!msg) continue;

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_EOS:
                std::cout << "Pipeline reported EOS\n";
                done = true;
                break;
            case GST_MESSAGE_ERROR: {
                GError *err = NULL;
                gchar *dbg = NULL;
                gst_message_parse_error(msg, &err, &dbg);
                std::cerr << "Error from element " << GST_OBJECT_NAME(msg->src) << ": "
                          << (err ? err->message : "unknown") << "\n";
                if (dbg) std::cerr << "Debug info: " << dbg << "\n";
                g_clear_error(&err);
                g_free(dbg);
                done = true;
                break;
            }
            default:
                break;
        }
        gst_message_unref(msg);
    }

    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(appsrc_elem); // from gst_bin_get_by_name
    gst_object_unref(pipeline);

    std::cout << "Finished — check out out.mp4 (may be unplayable without SPS/PPS).\n";
    return 0;
}