#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>

class QApplication;
class QWidget;

namespace lf::ui {

enum class Icon {
    Home,
    Continue,
    Tv,
    Movie,
    Collections,
    Settings,
    Play,
    Pause,
    Stop,
    Next,
    Previous,
    Rewind,
    Forward,
    VolumeUp,
    VolumeOff,
    Fullscreen,
    FullscreenExit,
    Subtitles,
    Audio,
    Speed,
    Back,
    Add,
    Folder,
    Edit,
    Delete,
    Check,
    More,
    Close,
    Image,
    Refresh,
    Warning,
    Replay
};

namespace Colors {
inline const QColor background(0x0f, 0x11, 0x15);
inline const QColor surface(0x17, 0x1a, 0x21);
inline const QColor surfaceRaised(0x1f, 0x23, 0x2c);
inline const QColor border(0x2b, 0x30, 0x3b);
inline const QColor text(0xe8, 0xea, 0xf0);
inline const QColor textMuted(0x9a, 0xa3, 0xb2);
inline const QColor accent(0x5b, 0x8d, 0xef);
inline const QColor accentHover(0x74, 0x9f, 0xf3);
inline const QColor success(0x3e, 0xcf, 0x8e);
inline const QColor warning(0xf5, 0xa5, 0x24);
inline const QColor danger(0xef, 0x53, 0x50);
}  // namespace Colors

namespace Theme {

// Fusion style + dark palette + application stylesheet.
void apply(QApplication& app);

// Vector icon (drawn from embedded SVG paths) tinted with `color`.
QPixmap iconPixmap(Icon icon, const QColor& color, int logicalSize, qreal devicePixelRatio = 1.0);
QIcon icon(Icon icon, const QColor& color = Colors::text, int logicalSize = 24);

// Cover/poster scaled and cropped to `size` (cover-fit). Falls back to a
// generated gradient placeholder that carries the title's initials.
QPixmap cover(const QString& imagePath, const QString& title, const QSize& size, qreal devicePixelRatio = 1.0);

// Enables the dark title bar on Windows 10/11 for this top-level window.
void useDarkTitleBar(QWidget* topLevel);

}  // namespace Theme

}  // namespace lf::ui
