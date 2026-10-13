#include "MpvWidget.h"

#include "MpvBackend.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QLoggingCategory>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QWheelEvent>

Q_LOGGING_CATEGORY(lcMpvWidget, "localflix.mpv.widget")

namespace lf {

namespace {

void* getProcAddress(void* /*ctx*/, const char* name)
{
    QOpenGLContext* gl = QOpenGLContext::currentContext();
    return gl ? reinterpret_cast<void*>(gl->getProcAddress(name)) : nullptr;
}

}  // namespace

MpvWidget::MpvWidget(MpvBackend* backend, QWidget* parent) : QOpenGLWidget(parent), m_backend(backend)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    // If the backend dies first, release the render context before the core goes away.
    connect(backend, &MpvBackend::aboutToDestroy, this, [this] { releaseContext(); }, Qt::DirectConnection);
}

MpvWidget::~MpvWidget()
{
    releaseContext();
}

void MpvWidget::releaseContext()
{
    if (!m_context) return;
    makeCurrent();
    // Stop update callbacks before freeing; the callback holds a raw `this`.
    mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
    mpv_render_context_free(m_context);
    m_context = nullptr;
    doneCurrent();
}

void MpvWidget::initializeGL()
{
    if (!m_backend || !m_backend->handle() || m_context) return;

    mpv_opengl_init_params glInit{};
    glInit.get_proc_address = &getProcAddress;
    glInit.get_proc_address_ctx = nullptr;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    const int rc = mpv_render_context_create(&m_context, m_backend->handle(), params);
    if (rc < 0) {
        qCCritical(lcMpvWidget) << "mpv_render_context_create failed:" << mpv_error_string(rc);
        m_context = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(m_context, &MpvWidget::updateCallback, this);
    qCInfo(lcMpvWidget) << "render context ready; GL" << reinterpret_cast<const char*>(context()->functions()->glGetString(GL_VERSION));
    emit renderReady();
}

void MpvWidget::updateCallback(void* ctx)
{
    // Any mpv thread. Fires at frame rate, so coalesce: at most one queued call
    // is outstanding; the slot clears the flag before repainting.
    auto* self = static_cast<MpvWidget*>(ctx);
    if (!self->m_redrawPending.exchange(true))
        QMetaObject::invokeMethod(self, "requestRedraw", Qt::QueuedConnection);
}

void MpvWidget::requestRedraw()
{
    m_redrawPending = false;
    update();
}

void MpvWidget::paintGL()
{
    QOpenGLFunctions* gl = context()->functions();
    if (!m_context) {
        gl->glClearColor(0.f, 0.f, 0.f, 1.f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{};
    fbo.fbo = int(defaultFramebufferObject());
    fbo.w = int(width() * dpr);
    fbo.h = int(height() * dpr);
    fbo.internal_format = 0;
    int flipY = 1;
    int blockForTarget = 0;  // never stall the GUI thread waiting for the vsync target
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTarget},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_context, params);
}

void MpvWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) emit doubleClicked();
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void MpvWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) emit clicked();
    QOpenGLWidget::mouseReleaseEvent(event);
}

void MpvWidget::mouseMoveEvent(QMouseEvent* event)
{
    emit mouseMoved();
    QOpenGLWidget::mouseMoveEvent(event);
}

void MpvWidget::wheelEvent(QWheelEvent* event)
{
    emit wheelScrolled(event->angleDelta().y());
    event->accept();
}

}  // namespace lf
