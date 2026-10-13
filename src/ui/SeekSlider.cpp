#include "SeekSlider.h"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

namespace lf::ui {

SeekSlider::SeekSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent)
{
    setRange(0, kMax);
    setProperty("seek", true);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(22);
}

double SeekSlider::fractionAt(int x) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    const int span = groove.width() - handle.width();
    if (span <= 0) return 0.0;
    const int pos = qBound(0, x - groove.x() - handle.width() / 2, span);
    return double(pos) / double(span);
}

void SeekSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return QSlider::mousePressEvent(e);
    m_scrubbing = true;
    const double f = fractionAt(int(e->position().x()));
    setValue(int(f * kMax));
    emit scrubbed(f);
    e->accept();
}

void SeekSlider::mouseMoveEvent(QMouseEvent* e)
{
    const double f = fractionAt(int(e->position().x()));
    if (m_scrubbing) {
        setValue(int(f * kMax));
        emit scrubbed(f);
    }
    emit hoverFraction(f, e->globalPosition().toPoint());
    e->accept();
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_scrubbing) return QSlider::mouseReleaseEvent(e);
    m_scrubbing = false;
    const double f = fractionAt(int(e->position().x()));
    setValue(int(f * kMax));
    emit seekRequested(f);
    e->accept();
}

void SeekSlider::leaveEvent(QEvent* e)
{
    emit hoverEnded();
    QSlider::leaveEvent(e);
}

}  // namespace lf::ui
