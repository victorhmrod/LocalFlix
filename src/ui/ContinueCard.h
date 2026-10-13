#pragma once

#include "Types.h"

#include <QFrame>

class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;

namespace lf::ui {

// Wide "Continue Watching" tile: cover, title, current episode, progress,
// last-watched info and a Resume button.
class ContinueCard : public QFrame {
    Q_OBJECT
public:
    struct Data {
        ContinueItem item;
        QString coverPath;  // resolved path, may be empty
        qint64 nowMs = 0;
    };

    explicit ContinueCard(QWidget* parent = nullptr);
    void setData(const Data& d);
    const ContinueItem& item() const { return m_item; }

signals:
    void resumeRequested();
    void openRequested();  // open the show page
    void menuRequested(const QPoint& globalPos);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    ContinueItem m_item;
    QLabel* m_cover;
    QLabel* m_title;
    QLabel* m_subtitle;
    QProgressBar* m_progress;
    QLabel* m_meta;
    QLabel* m_last;
    QPushButton* m_resume;
    QToolButton* m_more;
};

}  // namespace lf::ui
