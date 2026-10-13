#include "PosterCard.h"

#include "Theme.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace lf::ui {

namespace {
constexpr int kTextHeight = 62;
constexpr int kRadius = 12;
}  // namespace

PosterCard::PosterCard(QWidget* parent, const QSize& posterSize) : QWidget(parent), m_posterSize(posterSize)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
}

QSize PosterCard::sizeHint() const
{
    return QSize(m_posterSize.width() + 8, m_posterSize.height() + kTextHeight + 8);
}

void PosterCard::setData(const Data& d)
{
    m_data = d;
    setToolTip(d.subtitle.isEmpty() ? d.title : d.title + QStringLiteral("\n") + d.subtitle);
    setAccessibleName(d.title);
    update();
}

QRect PosterCard::posterRect() const
{
    return QRect(4, 4, m_posterSize.width(), m_posterSize.height());
}

QRect PosterCard::menuButtonRect() const
{
    const QRect p = posterRect();
    return QRect(p.right() - 36, p.top() + 8, 28, 28);
}

void PosterCard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect pr = posterRect();
    QPainterPath clip;
    clip.addRoundedRect(pr, kRadius, kRadius);

    p.save();
    p.setClipPath(clip);
    p.drawPixmap(pr.topLeft(), Theme::cover(m_data.imagePath, m_data.title, pr.size(), devicePixelRatioF()));

    if (m_hover || hasFocus()) {
        p.fillRect(pr, QColor(0, 0, 0, 90));
        // play glyph
        const int d = 52;
        const QRect circle(pr.center().x() - d / 2, pr.center().y() - d / 2, d, d);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawEllipse(circle);
        p.drawPixmap(circle.adjusted(13, 13, -13, -13).topLeft(), Theme::iconPixmap(Icon::Play, QColor(0x14, 0x16, 0x1b), 26, devicePixelRatioF()));
    }

    if (m_data.progress > 0.0 || m_data.watched) {
        const QRect bar(pr.left(), pr.bottom() - 4, pr.width(), 5);
        p.fillRect(bar, QColor(0, 0, 0, 160));
        const double f = m_data.watched ? 1.0 : qBound(0.0, m_data.progress, 1.0);
        p.fillRect(QRect(bar.left(), bar.top(), int(bar.width() * f), bar.height()), m_data.watched ? Colors::success : Colors::accent);
    }
    p.restore();

    // badges
    if (m_data.watched) {
        const QRect b(pr.left() + 8, pr.top() + 8, 24, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(Colors::success);
        p.drawEllipse(b);
        p.drawPixmap(b.adjusted(4, 4, -4, -4).topLeft(), Theme::iconPixmap(Icon::Check, QColor(0x0b, 0x1a, 0x12), 16, devicePixelRatioF()));
    } else if (m_data.missing) {
        const QRect b(pr.left() + 8, pr.top() + 8, 24, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(Colors::warning);
        p.drawEllipse(b);
        p.drawPixmap(b.adjusted(4, 4, -4, -4).topLeft(), Theme::iconPixmap(Icon::Warning, QColor(0x2a, 0x1c, 0x00), 16, devicePixelRatioF()));
    }
    if (m_hover || hasFocus()) {
        const QRect mb = menuButtonRect();
        p.setPen(Qt::NoPen);
        p.setBrush(m_overMenu ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 150));
        p.drawEllipse(mb);
        p.drawPixmap(mb.adjusted(2, 2, -2, -2).topLeft(), Theme::iconPixmap(Icon::More, Qt::white, 24, devicePixelRatioF()));
    }

    // border
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(hasFocus() ? Colors::accent : (m_hover ? QColor(255, 255, 255, 70) : QColor(255, 255, 255, 22)), hasFocus() ? 2 : 1));
    p.drawRoundedRect(QRectF(pr).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    // text
    const QRect textArea(pr.left() + 2, pr.bottom() + 8, pr.width() - 4, kTextHeight - 8);
    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(10.5);
    p.setFont(titleFont);
    p.setPen(Colors::text);
    const QFontMetrics tm(titleFont);
    p.drawText(QRect(textArea.left(), textArea.top(), textArea.width(), tm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               tm.elidedText(m_data.title, Qt::ElideRight, textArea.width()));
    QFont subFont = font();
    subFont.setPointSizeF(9.5);
    p.setFont(subFont);
    p.setPen(Colors::textMuted);
    const QFontMetrics sm(subFont);
    p.drawText(QRect(textArea.left(), textArea.top() + tm.height() + 2, textArea.width(), sm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               sm.elidedText(m_data.subtitle, Qt::ElideRight, textArea.width()));
}

void PosterCard::enterEvent(QEnterEvent*)
{
    m_hover = true;
    update();
}

void PosterCard::leaveEvent(QEvent*)
{
    m_hover = false;
    m_overMenu = false;
    m_pressed = false;
    update();
}

void PosterCard::mouseMoveEvent(QMouseEvent* e)
{
    const bool over = menuButtonRect().contains(e->position().toPoint());
    if (over != m_overMenu) {
        m_overMenu = over;
        update();
    }
}

void PosterCard::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) m_pressed = true;
}

void PosterCard::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_pressed) return;
    m_pressed = false;
    if (!rect().contains(e->position().toPoint())) return;
    if (menuButtonRect().contains(e->position().toPoint()))
        emit contextMenuRequested(e->globalPosition().toPoint());
    else
        emit clicked();
}

void PosterCard::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space)
        emit clicked();
    else if (e->key() == Qt::Key_Menu)
        emit contextMenuRequested(mapToGlobal(menuButtonRect().center()));
    else
        QWidget::keyPressEvent(e);
}

void PosterCard::contextMenuEvent(QContextMenuEvent* e)
{
    emit contextMenuRequested(e->globalPos());
}

void PosterCard::focusInEvent(QFocusEvent* e)
{
    QWidget::focusInEvent(e);
    update();
}

void PosterCard::focusOutEvent(QFocusEvent* e)
{
    QWidget::focusOutEvent(e);
    update();
}

}  // namespace lf::ui
