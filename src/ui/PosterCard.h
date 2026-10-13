#pragma once

#include <QWidget>

namespace lf::ui {

// A poster tile: cover art, title, subtitle, optional watched-progress bar and
// badges ("watched", "file missing"). Click = primary action, right click or
// the "..." button = context menu request.
class PosterCard : public QWidget {
    Q_OBJECT
public:
    struct Data {
        QString title;
        QString subtitle;
        QString imagePath;
        double progress = 0.0;  // 0..1 bar along the bottom of the poster
        bool watched = false;
        bool missing = false;
    };

    explicit PosterCard(QWidget* parent = nullptr, const QSize& posterSize = QSize(170, 255));
    void setData(const Data& d);
    const Data& data() const { return m_data; }
    QSize sizeHint() const override;

signals:
    void clicked();
    void contextMenuRequested(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    QRect posterRect() const;
    QRect menuButtonRect() const;

    Data m_data;
    QSize m_posterSize;
    bool m_hover = false;
    bool m_overMenu = false;
    bool m_pressed = false;
};

}  // namespace lf::ui
