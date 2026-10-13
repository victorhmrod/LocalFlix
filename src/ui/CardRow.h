#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace lf::ui {

// "Section title ........ See all" header above a horizontally scrolling row of
// cards. Owns the cards it is given.
class CardRow : public QWidget {
    Q_OBJECT
public:
    explicit CardRow(const QString& title, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSeeAllVisible(bool visible);
    void setEmptyText(const QString& text);
    // Replaces the current cards (ownership passes to the row).
    void setCards(const QList<QWidget*>& cards);
    int cardCount() const { return m_count; }

signals:
    void seeAllClicked();

private:
    QLabel* m_title;
    QPushButton* m_seeAll;
    QScrollArea* m_scroll;
    QWidget* m_content;
    QHBoxLayout* m_layout;
    QLabel* m_empty;
    int m_count = 0;
};

}  // namespace lf::ui
