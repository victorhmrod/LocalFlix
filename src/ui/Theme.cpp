#include "Theme.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFont>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmapCache>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QWidget>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace lf::ui {

namespace {

// Material Design icon paths (Apache License 2.0), 24x24 viewBox.
const char* pathFor(Icon i)
{
    switch (i) {
    case Icon::Home: return "M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z";
    case Icon::Continue:
        return "M13 3c-4.97 0-9 4.03-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42C8.27 19.99 10.51 21 13 21c4.97 0 9-4.03 9-9s-4.03-9-9-9zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z";
    case Icon::Tv:
        return "M21 3H3c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h5v2h8v-2h5c1.1 0 1.99-.9 1.99-2L23 5c0-1.1-.9-2-2-2zm0 14H3V5h18v12z";
    case Icon::Movie:
        return "M18 4l2 4h-3l-2-4h-2l2 4h-3l-2-4H8l2 4H7L5 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V4h-4z";
    case Icon::Collections:
        return "M4 6H2v14c0 1.1.9 2 2 2h14v-2H4V6zm16-4H8c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm-8 12.5v-9l6 4.5-6 4.5z";
    case Icon::Settings:
        return "M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.488.488 0 0 0-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 0 0-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z";
    case Icon::Play: return "M8 5v14l11-7z";
    case Icon::Pause: return "M6 19h4V5H6v14zm8-14v14h4V5h-4z";
    case Icon::Stop: return "M6 6h12v12H6z";
    case Icon::Next: return "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z";
    case Icon::Previous: return "M6 6h2v12H6zm3.5 6l8.5 6V6z";
    case Icon::Rewind: return "M11 18V6l-8.5 6 8.5 6zm.5-6l8.5 6V6l-8.5 6z";
    case Icon::Forward: return "M4 18l8.5-6L4 6v12zm9-12v12l8.5-6L13 6z";
    case Icon::VolumeUp:
        return "M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z";
    case Icon::VolumeOff:
        return "M16.5 12c0-1.77-1.02-3.29-2.5-4.03v2.21l2.45 2.45c.03-.2.05-.41.05-.63zm2.5 0c0 .94-.2 1.82-.54 2.64l1.51 1.51C20.63 14.91 21 13.5 21 12c0-4.28-2.99-7.86-7-8.77v2.06c2.89.86 5 3.54 5 6.71zM4.27 3L3 4.27 7.73 9H3v6h4l5 5v-6.73l4.25 4.25c-.67.52-1.42.93-2.25 1.18v2.06c1.38-.31 2.63-.95 3.69-1.81L19.73 21 21 19.73l-9-9L4.27 3zM12 4L9.91 6.09 12 8.18V4z";
    case Icon::Fullscreen: return "M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 7h-3v2h5v-5h-2v3zM14 5v2h3v3h2V5h-5z";
    case Icon::FullscreenExit: return "M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 11h2v-3h3v-2h-5v5zm2-11V5h-2v5h5V8h-3z";
    case Icon::Subtitles:
        return "M19 4H5c-1.11 0-2 .9-2 2v12c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm-8 7H9.5v-.5h-2v3h2V13H11v1c0 .55-.45 1-1 1H7c-.55 0-1-.45-1-1v-4c0-.55.45-1 1-1h3c.55 0 1 .45 1 1v1zm7 0h-1.5v-.5h-2v3h2V13H18v1c0 .55-.45 1-1 1h-3c-.55 0-1-.45-1-1v-4c0-.55.45-1 1-1h3c.55 0 1 .45 1 1v1z";
    case Icon::Audio: return "M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z";
    case Icon::Speed:
        return "M20.38 8.57l-1.23 1.85a8 8 0 0 1-.22 7.58H5.07A8 8 0 0 1 15.58 6.85l1.85-1.23A10 10 0 0 0 3.35 19a2 2 0 0 0 1.72 1h13.85a2 2 0 0 0 1.74-1 10 10 0 0 0-.27-10.44zm-9.79 6.84a2 2 0 0 0 2.83 0l5.66-8.49-8.49 5.66a2 2 0 0 0 0 2.83z";
    case Icon::Back: return "M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z";
    case Icon::Add: return "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z";
    case Icon::Folder:
        return "M20 6h-8l-2-2H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm0 12H4V8h16v10z";
    case Icon::Edit:
        return "M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34a.9959.9959 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z";
    case Icon::Delete: return "M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z";
    case Icon::Check: return "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z";
    case Icon::More:
        return "M12 8c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm0 2c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm0 6c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z";
    case Icon::Close:
        return "M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z";
    case Icon::Image:
        return "M21 19V5c0-1.1-.9-2-2-2H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2zM8.5 13.5l2.5 3.01L14.5 12l4.5 6H5l3.5-4.5z";
    case Icon::Refresh:
        return "M17.65 6.35A7.958 7.958 0 0 0 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0 1 12 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z";
    case Icon::Warning: return "M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z";
    case Icon::Replay:
        return "M12 5V1L7 6l5 5V7c3.31 0 6 2.69 6 6s-2.69 6-6 6-6-2.69-6-6H4c0 4.42 3.58 8 8 8s8-3.58 8-8-3.58-8-8-8z";
    case Icon::Flag: return "M14.4 6L14 4H5v17h2v-7h5.6l.4 2h7V6z";
    case Icon::PictureInPicture:
        return "M19 11h-8v6h8v-6zm4 8V4.98C23 3.88 22.1 3 21 3H3c-1.1 0-2 .88-2 1.98V19c0 1.1.9 2 2 2h18c1.1 0 2-.9 2-2zm-2 .02H3V4.97h18v14.05z";
    }
    return "";
}

QString styleSheet()
{
    const auto c = [](const QColor& col) { return col.name(); };
    QString css = QStringLiteral(R"css(
QWidget { color: %text%; font-size: 10pt; }
QMainWindow, QDialog, QMessageBox, QStackedWidget { background: %bg%; }
QLabel { background: transparent; }
QToolTip { background: %raised%; color: %text%; border: 1px solid %border%; padding: 4px 8px; }

QFrame#Sidebar { background: %surface%; border-right: 1px solid %border%; }
QLabel#Brand { font-size: 17pt; font-weight: 700; letter-spacing: 0.5px; }
QLabel#PageTitle { font-size: 20pt; font-weight: 700; }
QLabel#SectionTitle { font-size: 13pt; font-weight: 600; }
QLabel#Muted, QLabel[muted="true"] { color: %muted%; }
QLabel#Heading { font-size: 22pt; font-weight: 700; }

QPushButton, QToolButton {
    background: %raised%; border: 1px solid %border%; border-radius: 8px; padding: 7px 16px;
}
QPushButton:hover, QToolButton:hover { background: #272c37; border-color: #3a4150; }
QPushButton:pressed, QToolButton:pressed { background: #1a1e26; }
QPushButton:disabled, QToolButton:disabled { color: #5d6573; background: %surface%; border-color: %border%; }
QPushButton:focus { border-color: %accent%; }
QPushButton[variant="primary"] { background: %accent%; border-color: %accent%; color: #ffffff; font-weight: 600; }
QPushButton[variant="primary"]:hover { background: %accentHover%; border-color: %accentHover%; }
QPushButton[variant="primary"]:disabled { background: #2a3a5c; border-color: #2a3a5c; color: #7e8aa3; }
QPushButton[variant="ghost"], QToolButton[variant="ghost"] { background: transparent; border-color: transparent; }
QPushButton[variant="ghost"]:hover, QToolButton[variant="ghost"]:hover { background: rgba(255,255,255,0.08); }
QPushButton[variant="danger"] { background: transparent; border-color: %danger%; color: %danger%; }
QPushButton[variant="danger"]:hover { background: rgba(239,83,80,0.15); }
QToolButton[nav="true"] {
    background: transparent; border: none; border-radius: 10px; padding: 10px 14px; text-align: left; font-size: 11pt;
}
QToolButton[nav="true"]:hover { background: rgba(255,255,255,0.06); }
QToolButton[nav="true"]:checked { background: rgba(91,141,239,0.18); color: %accentHover%; font-weight: 600; }
QToolButton[iconButton="true"] { background: transparent; border: none; border-radius: 18px; padding: 6px; }
QToolButton[iconButton="true"]:hover { background: rgba(255,255,255,0.12); }
QToolButton[iconButton="true"]:pressed { background: rgba(255,255,255,0.20); }
QToolButton::menu-indicator { image: none; width: 0px; }

QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %surface%; border: 1px solid %border%; border-radius: 8px; padding: 6px 10px;
    selection-background-color: %accent%; selection-color: #ffffff;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %accent%; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: %raised%; border: 1px solid %border%; selection-background-color: %accent%; outline: none; padding: 4px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 18px; border: none; background: transparent; }

QCheckBox, QRadioButton { spacing: 10px; background: transparent; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 5px; border: 1px solid #4a5160; background: %surface%; }
QCheckBox::indicator:hover { border-color: %accent%; }
QCheckBox::indicator:checked { background: %accent%; border-color: %accent%; image: url(:/icons/check.svg); }
QCheckBox::indicator:disabled { background: %bg%; border-color: %border%; }

QSlider::groove:horizontal { height: 4px; background: #3a4050; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %accent%; border-radius: 2px; }
QSlider::handle:horizontal { background: #ffffff; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
QSlider::handle:horizontal:hover { background: %accentHover%; }
QSlider[seek="true"]::groove:horizontal { height: 6px; border-radius: 3px; background: rgba(255,255,255,0.28); }
QSlider[seek="true"]::sub-page:horizontal { border-radius: 3px; }
QSlider[seek="true"]::handle:horizontal { width: 14px; height: 14px; margin: -4px 0; border-radius: 7px; }

QProgressBar { background: #2a2f3a; border: none; border-radius: 3px; max-height: 6px; min-height: 6px; text-align: center; }
QProgressBar::chunk { background: %accent%; border-radius: 3px; }

QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: #353b48; border-radius: 4px; min-height: 36px; }
QScrollBar::handle:vertical:hover { background: #4a5263; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:horizontal { background: #353b48; border-radius: 4px; min-width: 36px; }
QScrollBar::handle:horizontal:hover { background: #4a5263; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }

QTreeView, QTreeWidget, QListView, QListWidget, QTableView {
    background: %surface%; border: 1px solid %border%; border-radius: 10px; outline: none; padding: 4px;
    alternate-background-color: #1a1d25;
}
QTreeView::item { padding: 5px 4px; border-radius: 0px; }
QListView::item { padding: 5px 4px; border-radius: 6px; }
QTreeView::item:hover, QListView::item:hover { background: rgba(255,255,255,0.05); }
QTreeView::item:selected, QListView::item:selected { background: rgba(91,141,239,0.28); color: %text%; }
QTreeView::branch { background: transparent; }
QHeaderView::section { background: %surface%; color: %muted%; border: none; border-bottom: 1px solid %border%; padding: 6px 8px; font-weight: 600; }

QMenu { background: %raised%; border: 1px solid %border%; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 7px 26px 7px 14px; border-radius: 6px; }
QMenu::item:selected { background: rgba(91,141,239,0.35); }
QMenu::item:disabled { color: #626a78; }
QMenu::separator { height: 1px; background: %border%; margin: 5px 8px; }
QMenu::indicator { width: 14px; height: 14px; }

QGroupBox { border: 1px solid %border%; border-radius: 12px; margin-top: 14px; padding: 18px 14px 12px 14px; background: %surface%; }
QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: %muted%; font-weight: 600; }
QSplitter::handle { background: transparent; }

QFrame#Card { background: %surface%; border: 1px solid %border%; border-radius: 14px; }
QFrame#Overlay { background: rgba(15,17,21,0.88); border: 1px solid rgba(255,255,255,0.10); border-radius: 16px; }
QFrame#ControlBar { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(0,0,0,0), stop:1 rgba(0,0,0,0.82)); border: none; }
QFrame#TopBar { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(0,0,0,0.9), stop:1 rgba(0,0,0,0)); border: none; }
QLabel#Toast { background: %raised%; border: 1px solid %border%; border-radius: 12px; padding: 12px 18px; }
)css");
    css.replace(QStringLiteral("%bg%"), c(Colors::background));
    css.replace(QStringLiteral("%surface%"), c(Colors::surface));
    css.replace(QStringLiteral("%raised%"), c(Colors::surfaceRaised));
    css.replace(QStringLiteral("%border%"), c(Colors::border));
    css.replace(QStringLiteral("%text%"), c(Colors::text));
    css.replace(QStringLiteral("%muted%"), c(Colors::textMuted));
    css.replace(QStringLiteral("%accentHover%"), c(Colors::accentHover));
    css.replace(QStringLiteral("%accent%"), c(Colors::accent));
    css.replace(QStringLiteral("%danger%"), c(Colors::danger));
    return css;
}

QColor placeholderColor(const QString& title, int variant)
{
    const QByteArray h = QCryptographicHash::hash(title.toUtf8(), QCryptographicHash::Md5);
    const int hue = (quint8(h[0]) * 360 / 256 + variant * 40) % 360;
    return QColor::fromHsl(hue, 90, 70);
}

QString initialsOf(const QString& title)
{
    QString out;
    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& w : words) {
        for (QChar ch : w) {
            if (ch.isLetterOrNumber()) {
                out += ch.toUpper();
                break;
            }
        }
        if (out.size() >= 2) break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

}  // namespace

namespace Theme {

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont f = app.font();
    f.setFamily(QStringLiteral("Segoe UI"));
    f.setPointSize(10);
    app.setFont(f);

    QPalette p;
    p.setColor(QPalette::Window, Colors::background);
    p.setColor(QPalette::WindowText, Colors::text);
    p.setColor(QPalette::Base, Colors::surface);
    p.setColor(QPalette::AlternateBase, QColor(0x1a, 0x1d, 0x25));
    p.setColor(QPalette::Text, Colors::text);
    p.setColor(QPalette::Button, Colors::surfaceRaised);
    p.setColor(QPalette::ButtonText, Colors::text);
    p.setColor(QPalette::ToolTipBase, Colors::surfaceRaised);
    p.setColor(QPalette::ToolTipText, Colors::text);
    p.setColor(QPalette::Highlight, Colors::accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, Colors::textMuted);
    p.setColor(QPalette::Link, Colors::accentHover);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x62, 0x6a, 0x78));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x62, 0x6a, 0x78));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x62, 0x6a, 0x78));
    app.setPalette(p);
    app.setStyleSheet(styleSheet());
}

QPixmap iconPixmap(Icon i, const QColor& color, int logicalSize, qreal dpr)
{
    const QString key = QStringLiteral("icon/%1/%2/%3/%4").arg(int(i)).arg(color.name(QColor::HexArgb)).arg(logicalSize).arg(dpr);
    QPixmap pm;
    if (QPixmapCache::find(key, &pm)) return pm;

    const QString svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path fill='%1' d='%2'/></svg>")
                            .arg(color.name(QColor::HexRgb), QString::fromLatin1(pathFor(i)));
    QSvgRenderer renderer(svg.toUtf8());
    const int px = int(logicalSize * dpr);
    pm = QPixmap(px, px);
    pm.fill(Qt::transparent);
    {
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter, QRectF(0, 0, px, px));
    }
    pm.setDevicePixelRatio(dpr);
    QPixmapCache::insert(key, pm);
    return pm;
}

QIcon icon(Icon i, const QColor& color, int logicalSize)
{
    QIcon out;
    out.addPixmap(iconPixmap(i, color, logicalSize, 1.0));
    out.addPixmap(iconPixmap(i, color, logicalSize, 2.0));
    return out;
}

QPixmap cover(const QString& imagePath, const QString& title, const QSize& size, qreal dpr)
{
    const QFileInfo fi(imagePath);
    const QString key = QStringLiteral("cover/%1/%2/%3/%4x%5/%6")
                            .arg(imagePath, title)
                            .arg(fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0)
                            .arg(size.width())
                            .arg(size.height())
                            .arg(dpr);
    QPixmap pm;
    if (QPixmapCache::find(key, &pm)) return pm;

    const QSize px(int(size.width() * dpr), int(size.height() * dpr));
    pm = QPixmap(px);
    pm.fill(Qt::transparent);

    QImage image;
    if (!imagePath.isEmpty() && fi.exists()) {
        QImageReader reader(imagePath);
        reader.setAutoTransform(true);
        const QSize src = reader.size();
        if (src.isValid()) {
            // decode close to the final size; cover-fit scales up to fill, so keep at least that size
            const QSize target = src.scaled(px, Qt::KeepAspectRatioByExpanding);
            if (target.width() < src.width()) reader.setScaledSize(target);
        }
        image = reader.read();
    }

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!image.isNull()) {
        const QImage scaled = image.scaled(px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QPoint topLeft((px.width() - scaled.width()) / 2, (px.height() - scaled.height()) / 2);
        painter.drawImage(topLeft, scaled);
    } else {
        QLinearGradient g(0, 0, px.width(), px.height());
        g.setColorAt(0.0, placeholderColor(title, 0).darker(230));
        g.setColorAt(1.0, placeholderColor(title, 1).darker(330));
        painter.fillRect(QRect(QPoint(0, 0), px), g);
        QFont f;
        f.setFamily(QStringLiteral("Segoe UI"));
        f.setBold(true);
        f.setPixelSize(int(px.height() * 0.28));
        painter.setFont(f);
        painter.setPen(QColor(255, 255, 255, 200));
        painter.drawText(QRect(QPoint(0, 0), px), Qt::AlignCenter, initialsOf(title));
    }
    painter.end();
    pm.setDevicePixelRatio(dpr);
    QPixmapCache::insert(key, pm);
    return pm;
}

void useDarkTitleBar(QWidget* topLevel)
{
#ifdef _WIN32
    if (!topLevel) return;
    const HWND hwnd = reinterpret_cast<HWND>(topLevel->winId());
    const BOOL on = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (19 on early Windows 10 builds)
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &on, sizeof(on)))) DwmSetWindowAttribute(hwnd, 19, &on, sizeof(on));
#else
    Q_UNUSED(topLevel)
#endif
}

}  // namespace Theme

}  // namespace lf::ui
