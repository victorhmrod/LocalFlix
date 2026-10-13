#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>

namespace lf::ui {

// Wraps its items into rows like text; used for the poster grids so they reflow
// when the window is resized.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = 0, int hSpacing = 16, int vSpacing = 20);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const { return m_hSpace; }
    int verticalSpacing() const { return m_vSpace; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    int count() const override { return int(m_items.size()); }
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override { return minimumSize(); }
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

}  // namespace lf::ui
