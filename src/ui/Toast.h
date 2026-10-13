#pragma once

#include <QLabel>
#include <QTimer>

namespace lf::ui {

// Transient message pinned to the bottom centre of its parent widget; used for
// state feedback ("Imported 5 episodes", errors, ...).
class Toast : public QLabel {
    Q_OBJECT
public:
    enum class Kind { Info, Success, Error };
    explicit Toast(QWidget* parent);
    void showMessage(const QString& text, Kind kind = Kind::Info, int durationMs = 4500);
    // Bottom (default) for library screens, top while the player is open.
    void setAnchoredToTop(bool top);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reposition();

    QTimer m_timer;
    bool m_top = false;
};

}  // namespace lf::ui
