#include "CenterCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace lf::ui {

CenterCard::CenterCard(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Overlay"));
    setFixedWidth(500);

    m_title = new QLabel(this);
    QFont f = m_title->font();
    f.setBold(true);
    f.setPointSizeF(15);
    m_title->setFont(f);
    m_title->setWordWrap(true);
    m_title->setAlignment(Qt::AlignCenter);

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setAlignment(Qt::AlignCenter);
    m_body->setProperty("muted", true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);

    m_buttons = new QHBoxLayout;
    m_buttons->setSpacing(10);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(12);
    root->addWidget(m_title);
    root->addWidget(m_body);
    root->addWidget(m_progress);
    root->addSpacing(6);
    root->addLayout(m_buttons);
    hide();
}

void CenterCard::present(const QString& title, const QString& body, const QList<Button>& buttons, bool withProgress)
{
    m_title->setText(title);
    m_body->setText(body);
    m_body->setVisible(!body.isEmpty());
    m_progress->setVisible(withProgress);
    m_progress->setValue(withProgress ? 1000 : 0);

    while (m_buttons->count() > 0) {
        QLayoutItem* item = m_buttons->takeAt(0);
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    m_buttons->addStretch();
    for (const Button& b : buttons) {
        auto* btn = new QPushButton(b.text, this);
        if (b.primary) btn->setProperty("variant", QStringLiteral("primary"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, [action = b.action] {
            if (action) action();
        });
        m_buttons->addWidget(btn);
    }
    m_buttons->addStretch();

    adjustSize();
    show();
    raise();
}

void CenterCard::setBody(const QString& body)
{
    m_body->setText(body);
    m_body->setVisible(!body.isEmpty());
}

void CenterCard::setProgress(int permille)
{
    m_progress->setValue(qBound(0, permille, 1000));
}

void CenterCard::dismiss()
{
    hide();
}

}  // namespace lf::ui
