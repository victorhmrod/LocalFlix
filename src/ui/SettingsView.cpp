#include "SettingsView.h"

#include "Theme.h"
#include "Toast.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace lf::ui {

SettingsView::SettingsView(ApplicationController& app, const QString& engineInfo, Toast* toast, QWidget* parent)
    : QWidget(parent), m_app(app), m_engineInfo(engineInfo), m_toast(toast)
{
    auto* content = new QWidget;
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(32, 28, 32, 32);
    root->setSpacing(18);

    auto* title = new QLabel(QStringLiteral("Settings"), content);
    title->setObjectName(QStringLiteral("PageTitle"));
    root->addWidget(title);

    auto note = [content](const QString& text) {
        auto* l = new QLabel(text, content);
        l->setProperty("muted", true);
        l->setWordWrap(true);
        return l;
    };

    // ---- playback
    auto* playback = new QGroupBox(QStringLiteral("Playback"), content);
    auto* pf = new QFormLayout(playback);
    pf->setSpacing(12);
    pf->setLabelAlignment(Qt::AlignLeft);
    m_autoPlay = new QCheckBox(QStringLiteral("Automatically play the next episode"), playback);
    m_countdown = new QSpinBox(playback);
    m_countdown->setRange(0, 30);
    m_countdown->setSuffix(QStringLiteral(" seconds"));
    m_countdown->setSpecialValueText(QStringLiteral("Immediately"));
    m_resume = new QCheckBox(QStringLiteral("Resume playback automatically (otherwise ask: resume or start over)"), playback);
    m_skipCompleted = new QCheckBox(QStringLiteral("Skip episodes I have already completed when advancing automatically"), playback);
    m_autoSkipIntro = new QCheckBox(QStringLiteral("Automatically skip openings (intros)"), playback);
    m_autoSkipEnding = new QCheckBox(QStringLiteral("Automatically skip endings (credits)"), playback);
    m_autoSkipIntro->setObjectName(QStringLiteral("AutoSkipIntro"));
    m_autoSkipEnding->setObjectName(QStringLiteral("AutoSkipEnding"));
    m_audioLang = new QLineEdit(playback);
    m_audioLang->setPlaceholderText(QStringLiteral("e.g. jpn  or  eng, por   (empty = file default)"));
    m_subLang = new QLineEdit(playback);
    m_subLang->setPlaceholderText(QStringLiteral("e.g. eng  or  por, eng   (empty = no automatic subtitles)"));
    m_volumeSlider = new QSlider(Qt::Horizontal, playback);
    m_volumeSlider->setRange(0, 130);
    m_volumeSpin = new QSpinBox(playback);
    m_volumeSpin->setRange(0, 130);
    m_volumeSpin->setSuffix(QStringLiteral(" %"));
    auto* volumeRow = new QHBoxLayout;
    volumeRow->addWidget(m_volumeSlider, 1);
    volumeRow->addWidget(m_volumeSpin);
    m_speed = new QComboBox(playback);
    for (double s : {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0}) m_speed->addItem(QStringLiteral("%1×").arg(s, 0, 'g', 3), s);
    pf->addRow(m_autoPlay);
    pf->addRow(QStringLiteral("Countdown before the next episode"), m_countdown);
    pf->addRow(m_resume);
    pf->addRow(m_skipCompleted);
    pf->addRow(m_autoSkipIntro);
    pf->addRow(m_autoSkipEnding);
    pf->addRow(note(QStringLiteral("Openings and endings are found through the chapters stored in the video file (titled Opening, OP, Intro, Ending, ED, Credits…). "
                                   "When the player is inside one, a <b>Skip</b> button appears (Enter also works); these options skip it by themselves. "
                                   "Skipping an ending that lasts until the end of the file finishes the episode, so the next one starts as usual. "
                                   "For files without such chapters, mark the ending (and the opening) once per season with the flag button in the player.")));
    pf->addRow(QStringLiteral("Preferred audio language"), m_audioLang);
    pf->addRow(QStringLiteral("Preferred subtitle language"), m_subLang);
    pf->addRow(QStringLiteral("Default volume"), volumeRow);
    pf->addRow(QStringLiteral("Default playback speed"), m_speed);
    pf->addRow(note(QStringLiteral("Languages use codes such as eng, por, jpn (comma-separated, in order of preference). "
                                   "The volume and speed apply to every new viewing session; you can still change them while watching.")));
    root->addWidget(playback);

    // ---- display
    auto* display = new QGroupBox(QStringLiteral("Display"), content);
    auto* df = new QFormLayout(display);
    df->setSpacing(12);
    m_fullscreen = new QComboBox(display);
    m_fullscreen->addItem(QStringLiteral("Manual: I toggle fullscreen myself"), int(FullscreenBehavior::Manual));
    m_fullscreen->addItem(QStringLiteral("Start playback in fullscreen"), int(FullscreenBehavior::OnPlayback));
    m_fullscreen->addItem(QStringLiteral("Remember the last state"), int(FullscreenBehavior::Remember));
    df->addRow(QStringLiteral("Fullscreen behaviour"), m_fullscreen);
    root->addWidget(display);

    // ---- library
    auto* library = new QGroupBox(QStringLiteral("Library"), content);
    auto* lf = new QFormLayout(library);
    lf->setSpacing(12);
    m_importDir = new QLineEdit(library);
    m_importDir->setReadOnly(true);
    m_importDir->setPlaceholderText(QStringLiteral("None: file dialogs open in your Videos folder"));
    auto* browse = new QPushButton(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Browse…"), library);
    auto* clear = new QPushButton(QStringLiteral("Clear"), library);
    clear->setProperty("variant", QStringLiteral("ghost"));
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(m_importDir, 1);
    dirRow->addWidget(browse);
    dirRow->addWidget(clear);
    lf->addRow(QStringLiteral("Default media import folder"), dirRow);

    auto* verify = new QPushButton(Theme::icon(Icon::Refresh, Colors::text, 18), QStringLiteral("Check all library files now"), library);
    m_verifyStatus = new QLabel(library);
    m_verifyStatus->setProperty("muted", true);
    auto* verifyRow = new QHBoxLayout;
    verifyRow->addWidget(verify);
    verifyRow->addWidget(m_verifyStatus, 1);
    lf->addRow(QStringLiteral("File check"), verifyRow);
    lf->addRow(note(QStringLiteral("LocalFlix never copies, moves or deletes your videos. This only looks for files that were moved, renamed or sit on a disconnected drive; "
                                   "reconnect them from the show page (right-click → Locate missing file).")));
    root->addWidget(library);

    // ---- about
    auto* about = new QGroupBox(QStringLiteral("About"), content);
    auto* al = new QVBoxLayout(about);
    m_about = new QLabel(about);
    m_about->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_about->setWordWrap(true);
    auto* openData = new QPushButton(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Open data folder"), about);
    auto* aboutRow = new QHBoxLayout;
    aboutRow->addWidget(openData);
    aboutRow->addStretch();
    al->addWidget(m_about);
    al->addLayout(aboutRow);
    root->addWidget(about);
    root->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // ---- write-through bindings
    auto& s = m_app.settings();
    connect(m_autoPlay, &QCheckBox::toggled, this, [this, &s](bool v) { if (!m_loading) { s.setAutoPlayNext(v); updateDependentControls(); } });
    connect(m_countdown, &QSpinBox::valueChanged, this, [this, &s](int v) { if (!m_loading) s.setAutoPlayCountdownSeconds(v); });
    connect(m_resume, &QCheckBox::toggled, this, [this, &s](bool v) { if (!m_loading) s.setResumeAutomatically(v); });
    connect(m_skipCompleted, &QCheckBox::toggled, this, [this, &s](bool v) { if (!m_loading) s.setSkipCompleted(v); });
    connect(m_autoSkipIntro, &QCheckBox::toggled, this, [this, &s](bool v) { if (!m_loading) s.setAutoSkipIntro(v); });
    connect(m_autoSkipEnding, &QCheckBox::toggled, this, [this, &s](bool v) { if (!m_loading) s.setAutoSkipEnding(v); });
    connect(m_audioLang, &QLineEdit::editingFinished, this, [this, &s] { if (!m_loading) s.setPreferredAudioLanguage(m_audioLang->text()); });
    connect(m_subLang, &QLineEdit::editingFinished, this, [this, &s] { if (!m_loading) s.setPreferredSubtitleLanguage(m_subLang->text()); });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this, &s](int v) {
        if (m_loading) return;
        m_loading = true;
        m_volumeSpin->setValue(v);
        m_loading = false;
        s.setDefaultVolume(v);
    });
    connect(m_volumeSpin, &QSpinBox::valueChanged, this, [this, &s](int v) {
        if (m_loading) return;
        m_loading = true;
        m_volumeSlider->setValue(v);
        m_loading = false;
        s.setDefaultVolume(v);
    });
    connect(m_speed, &QComboBox::activated, this, [this, &s](int i) { if (!m_loading) s.setPlaybackSpeed(m_speed->itemData(i).toDouble()); });
    connect(m_fullscreen, &QComboBox::activated, this, [this, &s](int i) { if (!m_loading) s.setFullscreenBehavior(FullscreenBehavior(m_fullscreen->itemData(i).toInt())); });
    connect(browse, &QPushButton::clicked, this, [this, &s] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Default media import folder"), s.defaultImportDir());
        if (dir.isEmpty()) return;
        s.setDefaultImportDir(dir);
        m_importDir->setText(QDir::toNativeSeparators(dir));
    });
    connect(clear, &QPushButton::clicked, this, [this, &s] {
        s.setDefaultImportDir(QString());
        m_importDir->clear();
    });
    connect(verify, &QPushButton::clicked, this, [this] {
        m_verifyStatus->setText(QStringLiteral("Checking…"));
        m_app.library().verifyFilesAsync(m_app.library().allMediaFiles());
    });
    connect(&m_app.library(), &LibraryManager::verificationFinished, this, [this](int checked, int missing) {
        m_verifyStatus->setText(missing == 0 ? QStringLiteral("All files found (%1 checked)").arg(checked)
                                             : QStringLiteral("%1 missing of %2 checked").arg(missing).arg(checked));
    });
    connect(openData, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_app.dataDir())); });

    load();
}

void SettingsView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    load();
}

void SettingsView::load()
{
    m_loading = true;
    auto& s = m_app.settings();
    m_autoPlay->setChecked(s.autoPlayNext());
    m_countdown->setValue(s.autoPlayCountdownSeconds());
    m_resume->setChecked(s.resumeAutomatically());
    m_skipCompleted->setChecked(s.skipCompleted());
    m_autoSkipIntro->setChecked(s.autoSkipIntro());
    m_autoSkipEnding->setChecked(s.autoSkipEnding());
    m_audioLang->setText(s.preferredAudioLanguage());
    m_subLang->setText(s.preferredSubtitleLanguage());
    m_volumeSlider->setValue(s.defaultVolume());
    m_volumeSpin->setValue(s.defaultVolume());
    int speedIndex = 2;
    for (int i = 0; i < m_speed->count(); ++i)
        if (qAbs(m_speed->itemData(i).toDouble() - s.playbackSpeed()) < 0.001) speedIndex = i;
    m_speed->setCurrentIndex(speedIndex);
    for (int i = 0; i < m_fullscreen->count(); ++i)
        if (m_fullscreen->itemData(i).toInt() == int(s.fullscreenBehavior())) m_fullscreen->setCurrentIndex(i);
    m_importDir->setText(QDir::toNativeSeparators(s.defaultImportDir()));
    m_loading = false;
    updateDependentControls();
    updateAbout();
}

void SettingsView::updateDependentControls()
{
    m_countdown->setEnabled(m_autoPlay->isChecked());
    m_skipCompleted->setEnabled(m_autoPlay->isChecked());
}

void SettingsView::updateAbout()
{
    auto& lib = m_app.library();
    m_about->setText(QStringLiteral("<b>LocalFlix</b> %1 · offline media library and player<br>"
                                    "Playback engine: %2<br>Qt %3 · database schema v%4<br>"
                                    "Library: %5 shows · %6 movies · %7 collections<br>"
                                    "Data folder: %8")
                         .arg(QCoreApplication::applicationVersion(), m_engineInfo.toHtmlEscaped(), QString::fromLatin1(qVersion()))
                         .arg(m_app.database().schemaVersion())
                         .arg(lib.showSummaries().size())
                         .arg(lib.movies().size())
                         .arg(lib.collections().size())
                         .arg(QDir::toNativeSeparators(m_app.dataDir()).toHtmlEscaped()));
}

}  // namespace lf::ui
