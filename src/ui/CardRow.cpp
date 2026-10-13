#include "CardRow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace lf::ui {

CardRow::CardRow(const QString& title, QWidget* parent) : QWidget(parent)
{
    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("SectionTitle"));
    m_seeAll = new QPushButton(QStringLiteral("See all"), this);
    m_seeAll->setProperty("variant", QStringLiteral("ghost"));
    m_seeAll->setCursor(Qt::PointingHandCursor);
    m_seeAll->hide();
    connect(m_seeAll, &QPushButton::clicked, this, &CardRow::seeAllClicked);

    auto* header = new QHBoxLayout;
    header->addWidget(m_title);
    header->addStretch();
    header->addWidget(m_seeAll);

    m_content = new QWidget;
    m_layout = new QHBoxLayout(m_content);
    m_layout->setContentsMargins(0, 4, 0, 4);
    m_layout->setSpacing(16);
    m_layout->addStretch();

    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_content);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_content->setAutoFillBackground(false);

    m_empty = new QLabel(this);
    m_empty->setProperty("muted", true);
    m_empty->hide();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    root->addLayout(header);
    root->addWidget(m_scroll);
    root->addWidget(m_empty);
}

void CardRow::setTitle(const QString& title)
{
    m_title->setText(title);
}

void CardRow::setSeeAllVisible(bool visible)
{
    m_seeAll->setVisible(visible);
}

void CardRow::setEmptyText(const QString& text)
{
    m_empty->setText(text);
}

void CardRow::setCards(const QList<QWidget*>& cards)
{
    while (m_layout->count() > 0) {
        QLayoutItem* item = m_layout->takeAt(0);
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    m_count = int(cards.size());
    for (QWidget* w : cards) m_layout->addWidget(w);
    m_layout->addStretch();

    const bool any = !cards.isEmpty();
    m_scroll->setVisible(any);
    m_empty->setVisible(!any && !m_empty->text().isEmpty());
    if (any) {
        int h = 0;
        for (QWidget* w : cards) h = qMax(h, w->sizeHint().height());
        // room for the horizontal scrollbar so cards are never clipped
        m_scroll->setFixedHeight(h + 8 + m_scroll->horizontalScrollBar()->sizeHint().height());
    }
    m_scroll->horizontalScrollBar()->setValue(0);
}

}  // namespace lf::ui
