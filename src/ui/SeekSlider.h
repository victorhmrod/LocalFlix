#pragma once

#include <QSlider>

namespace lf::ui {

// Horizontal slider for the playback position: a click jumps straight to that
// spot (a stock QSlider only pages), dragging previews, release seeks.
// Values are in 1/10000ths of the duration.
class SeekSlider : public QSlider {
    Q_OBJECT
public:
    static constexpr int kMax = 10000;
    explicit SeekSlider(QWidget* parent = nullptr);

    bool isScrubbing() const { return m_scrubbing; }

signals:
    void scrubbed(double fraction);        // while dragging
    void seekRequested(double fraction);   // on release / click
    void hoverFraction(double fraction, const QPoint& globalPos);
    void hoverEnded();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    double fractionAt(int x) const;

    bool m_scrubbing = false;
};

}  // namespace lf::ui
