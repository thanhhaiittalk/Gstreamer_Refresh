// appsink_example_fixed.cpp
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <iostream>
#include <string>
#include <cstring>

// Helper: format integer with commas
static std::string format_with_commas(uint64_t v) {
    std::string s = std::to_string(v);
    int insertPosition = (int)s.length() - 3;
    while (insertPosition > 0) {
        s.insert(insertPosition, ",");
        insertPosition -= 3;
    }
    return s;
}

static GstFlowReturn on_new_sample(GstAppSink *appsink, gpointer user_data) {
    // defensive pull
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) {
        std::cerr << "[on_new_sample] Failed to pull sample\n";
        return GST_FLOW_ERROR;
    }

    GstBuffer *buf = gst_sample_get_buffer(sample);
    if (!buf) {
        std::cerr << "[on_new_sample] sample has no buffer\n";
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    GstClockTime pts = GST_BUFFER_PTS(buf);
    if (GST_CLOCK_TIME_IS_VALID(pts)) {
        std::cout << "pts = " << format_with_commas((uint64_t)pts) << " ns\n";
    } else {
        std::cout << "pts = (none)\n";
    }

    gst_sample_unref(sample);

    // if you want to quit after N frames, you can use user_data to hold GMainLoop*
    // GMainLoop *loop = static_cast<GMainLoop*>(user_data);
    // if (some_condition) g_main_loop_quit(loop);

    return GST_FLOW_OK;
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    GError *error = nullptr;
    const char *pipeline_desc =
      "videotestsrc is-live=true ! videoconvert ! x264enc tune=zerolatency bitrate=500 speed-preset=superfast ! h264parse ! appsink name=s";

    GstElement *pipeline = gst_parse_launch(pipeline_desc, &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return 1;
    }

    // create main loop early so bus callbacks can quit it
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);

    // print children for debugging (optional)
    GList *children = GST_BIN(pipeline)->children;
    for (GList *l = children; l; l = l->next) {
        GstElement *e = GST_ELEMENT(l->data);
        std::cout << "element: " << gst_element_get_name(e)
                  << " factory: " << (gst_element_get_factory(e) ? gst_plugin_feature_get_name((GstPluginFeature*)gst_element_get_factory(e)) : "unknown")
                  << "\n";
    }

    // Get appsink by name
    GstElement *sink_elem = gst_bin_get_by_name(GST_BIN(pipeline), "s");
    if (!sink_elem) {
        std::cerr << "Failed to find appsink named 's'\n";
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        g_main_loop_unref(loop);
        return 1;
    }

    if (!GST_IS_APP_SINK(sink_elem)) {
        std::cerr << "'s' is not an appsink (type mismatch)\n";
        gst_object_unref(sink_elem);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        g_main_loop_unref(loop);
        return 1;
    }

    GstAppSink *appsink = GST_APP_SINK(sink_elem);

    // Configure appsink
    gst_app_sink_set_emit_signals(appsink, FALSE);
    gst_app_sink_set_max_buffers(appsink, 1);
    gst_app_sink_set_drop(appsink, TRUE);

    // Zero-init callbacks (CRITICAL)
    GstAppSinkCallbacks callbacks;
    std::memset(&callbacks, 0, sizeof(callbacks));
    callbacks.new_sample = on_new_sample;
    gst_app_sink_set_callbacks(appsink, &callbacks, loop, nullptr);

    // we can drop our local reference to sink_elem now
    gst_object_unref(sink_elem);

    // watch bus for runtime errors/warnings
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, [](GstBus *bus, GstMessage *msg, gpointer data) -> gboolean {
        GMainLoop *loop = static_cast<GMainLoop*>(data);
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError *err = nullptr; gchar *dbg = nullptr;
                gst_message_parse_error(msg, &err, &dbg);
                std::cerr << "[BUS] ERROR: " << (err ? err->message : "unknown") << "\n";
                if (dbg) { std::cerr << "Debug: " << dbg << "\n"; g_free(dbg); }
                if (err) g_error_free(err);
                if (loop) g_main_loop_quit(loop);
                break;
            }
            case GST_MESSAGE_WARNING: {
                GError *warn = nullptr; gchar *dbg = nullptr;
                gst_message_parse_warning(msg, &warn, &dbg);
                std::cerr << "[BUS] WARNING: " << (warn ? warn->message : "unknown") << "\n";
                if (dbg) { std::cerr << "Debug: " << dbg << "\n"; g_free(dbg); }
                if (warn) g_error_free(warn);
                break;
            }
            case GST_MESSAGE_EOS:
                if (loop) g_main_loop_quit(loop);
                break;
            default: break;
        }
        return TRUE;
    }, loop);

    // Start pipeline
    GstStateChangeReturn sret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (sret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to set pipeline to PLAYING\n";
        gst_object_unref(bus);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        g_main_loop_unref(loop);
        return 1;
    }

    std::cout << "Running — press Ctrl+C to stop\n";
    g_main_loop_run(loop);

    // cleanup
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return 0;
}
