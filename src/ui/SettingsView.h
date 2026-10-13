#pragma once

#include "ApplicationController.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;

namespace lf::ui {

class Toast;

// Persistent application settings. Every control writes through to the
// SettingsManager immediately; nothing needs an "Apply" button.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    SettingsView(ApplicationController& app, const QString& engineInfo, Toast* toast, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void load();
    void updateAbout();
    void updateDependentControls();

    ApplicationController& m_app;
    QString m_engineInfo;
    Toast* m_toast;
    bool m_loading = false;

    QCheckBox* m_autoPlay;
    QSpinBox* m_countdown;
    QCheckBox* m_resume;
    QCheckBox* m_skipCompleted;
    QLineEdit* m_audioLang;
    QLineEdit* m_subLang;
    QSlider* m_volumeSlider;
    QSpinBox* m_volumeSpin;
    QComboBox* m_speed;
    QComboBox* m_fullscreen;
    QLineEdit* m_importDir;
    QLabel* m_verifyStatus;
    QLabel* m_about;
};

}  // namespace lf::ui
