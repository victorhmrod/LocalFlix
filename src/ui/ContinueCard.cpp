#include "ContinueCard.h"

#include "Format.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
const QSize kCoverSize(100, 150);
}

ContinueCard::ContinueCard(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Card"));
    setFixedSize(440, 174);
    setCursor(Qt::PointingHandCursor);

    m_cover = new QLabel(this);
    m_cover->setFixedSize(kCoverSize);

    m_title = new QLabel(this);
    QFont tf = m_title->font();
    tf.setBold(true);
    tf.setPointSizeF(12);
    m_title->setFont(tf);
    m_subtitle = new QLabel(this);
    m_subtitle->setProperty("muted", true);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_meta = new QLabel(this);
    m_meta->setProperty("muted", true);
    m_last = new QLabel(this);
    m_last->setProperty("muted", true);

    m_resume = new QPushButton(Theme::icon(Icon::Play, Qt::white, 18), QStringLiteral("Resume"), this);
    m_resume->setProperty("variant", QStringLiteral("primary"));
    m_resume->setObjectName(QStringLiteral("ResumeButton"));
    m_resume->setCursor(Qt::PointingHandCursor);
    m_more = new QToolButton(this);
    m_more->setIcon(Theme::icon(Icon::More, Colors::text, 20));
    m_more->setProperty("iconButton", true);
    m_more->setToolTip(QStringLiteral("More options"));
    m_more->setCursor(Qt::PointingHandCursor);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_resume);
    buttons->addWidget(m_more);
    buttons->addStretch();

    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    text->addWidget(m_title);
    text->addWidget(m_subtitle);
    text->addSpacing(6);
    text->addWidget(m_progress);
    text->addWidget(m_meta);
    text->addWidget(m_last);
    text->addStretch();
    text->addLayout(buttons);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 14, 12);
    root->setSpacing(14);
    root->addWidget(m_cover, 0, Qt::AlignTop);
    root->addLayout(text, 1);

    connect(m_resume, &QPushButton::clicked, this, &ContinueCard::resumeRequested);
    connect(m_more, &QToolButton::clicked, this, [this] { emit menuRequested(m_more->mapToGlobal(QPoint(0, m_more->height()))); });
}

void ContinueCard::setData(const Data& d)
{
    m_item = d.item;
    const ContinueItem& it = d.item;
    m_cover->setPixmap(Theme::cover(d.coverPath, it.title, kCoverSize, devicePixelRatioF()));

    const QFontMetrics fm(m_title->font());
    const int textWidth = width() - kCoverSize.width() - 12 - 14 - 14;
    m_title->setText(fm.elidedText(it.title, Qt::ElideRight, textWidth));
    m_title->setToolTip(it.title);
    m_subtitle->setText(it.subtitle.isEmpty() ? QStringLiteral("Movie") : QFontMetrics(m_subtitle->font()).elidedText(it.subtitle, Qt::ElideRight, textWidth));

    m_progress->setValue(int(it.fraction() * 1000));
    if (it.positionMs > 0 && it.durationMs > 0) {
        const qint64 left = qMax<qint64>(0, it.durationMs - it.positionMs);
        m_meta->setText(QStringLiteral("%1% watched · %2 left").arg(int(it.fraction() * 100.0 + 0.5)).arg(formatClock(left)));
    } else if (it.positionMs > 0) {
        m_meta->setText(QStringLiteral("Resume at %1").arg(formatClock(it.positionMs)));
    } else {
        m_meta->setText(QStringLiteral("Up next · starts from the beginning"));
    }
    m_last->setText(QStringLiteral("Last watched: %1").arg(formatRelativeTime(it.lastWatchedAt, d.nowMs)));

    if (it.missing) {
        m_resume->setText(QStringLiteral("Fix missing file"));
        m_resume->setIcon(Theme::icon(Icon::Warning, Qt::white, 18));
    } else {
        m_resume->setText(it.positionMs > 0 ? QStringLiteral("Resume") : QStringLiteral("Play"));
        m_resume->setIcon(Theme::icon(Icon::Play, Qt::white, 18));
    }
}

void ContinueCard::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit resumeRequested();
    QFrame::mouseReleaseEvent(e);
}

void ContinueCard::contextMenuEvent(QContextMenuEvent* e)
{
    emit menuRequested(e->globalPos());
}

}  // namespace lf::ui
