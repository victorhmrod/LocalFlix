#pragma once

#include <QOpenGLWidget>

#include <atomic>

struct mpv_render_context;

namespace lf {

class MpvBackend;

// Video surface: a QOpenGLWidget into which libmpv renders through its render
// API. Sibling widgets can be stacked above it (controls, overlays) because
// the pixels are composited by Qt rather than by a foreign native window.
//
// Never reparent this widget: that would recreate its GL context and orphan
// the libmpv render context.
class MpvWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvWidget(MpvBackend* backend, QWidget* parent = nullptr);
    ~MpvWidget() override;

    bool isRenderReady() const { return m_context != nullptr; }

signals:
    // The libmpv render context exists; media can now be loaded with video.
    void renderReady();
    void doubleClicked();
    void clicked();
    void mouseMoved();
    void wheelScrolled(int angleDeltaY);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void requestRedraw();

private:
    static void updateCallback(void* ctx);
    void releaseContext();

    MpvBackend* m_backend;
    mpv_render_context* m_context = nullptr;
    std::atomic<bool> m_redrawPending{false};
};

}  // namespace lf
