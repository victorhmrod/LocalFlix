#include "FlowLayout.h"

#include <QWidget>

namespace lf::ui {

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0)) delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : std::as_const(m_items)) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect effective = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;

    for (QLayoutItem* item : std::as_const(m_items)) {
        if (item->isEmpty()) continue;
        const QSize hint = item->sizeHint();
        int nextX = x + hint.width() + m_hSpace;
        if (nextX - m_hSpace > effective.right() + 1 && lineHeight > 0) {
            x = effective.x();
            y = y + lineHeight + m_vSpace;
            nextX = x + hint.width() + m_hSpace;
            lineHeight = 0;
        }
        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), hint));
        x = nextX;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

}  // namespace lf::ui
