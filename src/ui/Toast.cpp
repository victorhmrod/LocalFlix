#include "Toast.h"

#include "Theme.h"

#include <QEvent>

namespace lf::ui {

Toast::Toast(QWidget* parent) : QLabel(parent)
{
    setObjectName(QStringLiteral("Toast"));
    setWordWrap(true);
    setAlignment(Qt::AlignCenter);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setMaximumWidth(640);
    hide();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QWidget::hide);
    parent->installEventFilter(this);
}

void Toast::showMessage(const QString& text, Kind kind, int durationMs)
{
    const QColor accent = kind == Kind::Error ? Colors::danger : (kind == Kind::Success ? Colors::success : Colors::accent);
    setStyleSheet(QStringLiteral("QLabel#Toast { border-left: 4px solid %1; }").arg(accent.name()));
    setText(text);
    adjustSize();
    reposition();
    raise();
    show();
    m_timer.start(durationMs);
}

void Toast::setAnchoredToTop(bool top)
{
    m_top = top;
    if (isVisible()) reposition();
}

void Toast::reposition()
{
    QWidget* p = parentWidget();
    if (!p) return;
    const int w = qMin(maximumWidth(), p->width() - 48);
    setFixedWidth(w);
    adjustSize();
    move((p->width() - width()) / 2, m_top ? 104 : p->height() - height() - 28);
}

bool Toast::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) reposition();
    return QLabel::eventFilter(watched, event);
}

}  // namespace lf::ui
