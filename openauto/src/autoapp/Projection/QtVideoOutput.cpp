#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include <f1x/openauto/autoapp/Projection/QtVideoOutput.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x {
namespace openauto {
namespace autoapp {
namespace projection {

static void ensure_gst_init_once(bool& inited)
{
    if (!inited) {
        int argc = 0;
        char** argv = nullptr;
        gst_init(&argc, &argv);
        inited = true;
    }
}

QtVideoOutput::QtVideoOutput(configuration::IConfiguration::Pointer configuration)
    : VideoOutput(std::move(configuration))
{
    this->moveToThread(QApplication::instance()->thread());
    connect(this, &QtVideoOutput::startPlayback, this, &QtVideoOutput::onStartPlayback, Qt::BlockingQueuedConnection);
    connect(this, &QtVideoOutput::stopPlayback,  this, &QtVideoOutput::onStopPlayback,  Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(this, "createVideoOutput", Qt::BlockingQueuedConnection);
}

QtVideoOutput::~QtVideoOutput()
{
    OPENAUTO_LOG(info) << "[QtVideoOutput] Destructor";
    cleanupPlayer();
}

void QtVideoOutput::createVideoOutput()
{
    OPENAUTO_LOG(info) << "[QtVideoOutput] createVideoOutput()";
    videoWidget_ = std::make_unique<QWidget>();
    videoWidget_->setAttribute(Qt::WA_NativeWindow, true);
    videoWidget_->setAttribute(Qt::WA_NoSystemBackground, true);
    videoWidget_->setAttribute(Qt::WA_OpaquePaintEvent, true);
}

bool QtVideoOutput::open()
{
    return true;
}

bool QtVideoOutput::init()
{
    emit startPlayback();
    return true;
}

void QtVideoOutput::stop()
{
    emit stopPlayback();
}

bool QtVideoOutput::ensurePipeline()
{
    if (pipeline_ && appsrc_ && videosink_) return true;

    ensure_gst_init_once(gstInited_);

    // appsrc (H264 byte-stream) -> h264parse -> avdec_h264 -> videoconvert -> ximagesink (overlay)
    //
    const char* pipelineDesc =
        "appsrc name=src is-live=true format=time do-timestamp=true "
        "! queue "
        "! h264parse config-interval=-1 "
        "! avdec_h264 "
        "! videoconvert "
        "! autovideosink name=vsink sync=false";

    GError* err = nullptr;
    pipeline_ = gst_parse_launch(pipelineDesc, &err);
    if (!pipeline_) {
        OPENAUTO_LOG(error) << "[QtVideoOutput] gst_parse_launch failed: "
                            << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
    videosink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "vsink");

    if (!appsrc_ || !videosink_) {
        OPENAUTO_LOG(error) << "[QtVideoOutput] Failed to get appsrc/videosink from pipeline";
        cleanupPlayer();
        return false;
    }

    GstBus* bus = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus, +[](GstBus*, GstMessage* msg, gpointer selfPtr) -> gboolean {
        auto* self = static_cast<QtVideoOutput*>(selfPtr);

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err=nullptr; gchar* dbg=nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            OPENAUTO_LOG(error) << "[GStreamer] ERROR: " << (err?err->message:"")
                                << " dbg=" << (dbg?dbg:"");
            if(err) g_error_free(err);
            if(dbg) g_free(dbg);
            break;
        }
        case GST_MESSAGE_ELEMENT: {
            const GstStructure* s = gst_message_get_structure(msg);
            if (s && gst_structure_has_name(s, "prepare-window-handle")) {
                WId wid = self->videoWidget_->winId();
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(self->videosink_), (guintptr)wid);
                gst_video_overlay_handle_events(GST_VIDEO_OVERLAY(self->videosink_), TRUE);
            }
            break;
        }
        default: break;
        }
        return TRUE;
    }, this);
    gst_object_unref(bus);

    GstCaps* caps = gst_caps_new_simple("video/x-h264",
       "stream-format", G_TYPE_STRING, "byte-stream",
       "alignment",     G_TYPE_STRING, "au",
       nullptr);

    g_object_set(G_OBJECT(appsrc_),
                 "caps", caps,
                 "block", FALSE,
                 nullptr);
    gst_caps_unref(caps);

    return true;
}

void QtVideoOutput::onStartPlayback()
{
    OPENAUTO_LOG(info) << "[QtVideoOutput] onStartPlayback()";

    if (!ensurePipeline()) {
        OPENAUTO_LOG(error) << "[QtVideoOutput] Pipeline creation failed";
        return;
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect g = screen->geometry();
        videoWidget_->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        videoWidget_->setGeometry(g);
        OPENAUTO_LOG(info) << "[QtVideoOutput] Widget geometry: "
                           << g.width() << "x" << g.height()
                           << " at (" << g.x() << "," << g.y() << ")";
    } else {
        videoWidget_->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        videoWidget_->showFullScreen();
        OPENAUTO_LOG(warning) << "[QtVideoOutput] No screen detected, using showFullScreen()";
    }

    videoWidget_->show();
    videoWidget_->raise();
    videoWidget_->activateWindow();

    // embed sink into Qt window using VideoOverlay
    WId wid = videoWidget_->winId();
//    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videosink_), static_cast<guintptr>(wid));

    // Start pipeline
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        OPENAUTO_LOG(error) << "[QtVideoOutput] Failed to set pipeline to PLAYING";
        cleanupPlayer();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        playerReady_ = true;
    }

    OPENAUTO_LOG(info) << "[QtVideoOutput] Pipeline set to PLAYING, ready to receive buffers";
}

void QtVideoOutput::write(uint64_t /*timestamp*/, const aasdk::common::DataConstBuffer& buffer)
{
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (!playerReady_ || !appsrc_) return;

    // Create GstBuffer and push into appsrc
    GstBuffer* gstbuf = gst_buffer_new_allocate(nullptr, buffer.size, nullptr);
    if (!gstbuf) return;

    gst_buffer_fill(gstbuf, 0, buffer.cdata, buffer.size);

    GstFlowReturn fr = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), gstbuf);
    if (fr != GST_FLOW_OK) {
        OPENAUTO_LOG(error) << "[QtVideoOutput] gst_app_src_push_buffer failed: " << fr;
        playerReady_ = false;
    }
}

void QtVideoOutput::onStopPlayback()
{
    OPENAUTO_LOG(info) << "[QtVideoOutput] onStopPlayback()";
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        playerReady_ = false;
    }
    cleanupPlayer();
}

void QtVideoOutput::cleanupPlayer()
{
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    if (appsrc_) {
        gst_object_unref(appsrc_);
        appsrc_ = nullptr;
    }
    if (videosink_) {
        gst_object_unref(videosink_);
        videosink_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    if (videoWidget_) {
        videoWidget_->hide();
    }
}

} } } }
