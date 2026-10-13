#pragma once

#include <QFrame>

#include <functional>

class QHBoxLayout;
class QLabel;
class QProgressBar;

namespace lf::ui {

// Centered card over the video for transitions and problems: "Next episode in 3",
// "File not found", "Finished"... Each state supplies its own action buttons.
class CenterCard : public QFrame {
    Q_OBJECT
public:
    struct Button {
        QString text;
        bool primary = false;
        std::function<void()> action;
    };

    explicit CenterCard(QWidget* parent = nullptr);

    void present(const QString& title, const QString& body, const QList<Button>& buttons, bool withProgress = false);
    void setBody(const QString& body);
    void setProgress(int permille);
    void dismiss();
    bool isPresented() const { return isVisible(); }

private:
    QLabel* m_title;
    QLabel* m_body;
    QProgressBar* m_progress;
    QHBoxLayout* m_buttons;
};

}  // namespace lf::ui
