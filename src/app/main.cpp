#include "ApplicationController.h"
#include "MainWindow.h"
#include "MpvBackend.h"
#include "Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <memory>

namespace {

// Appends warnings and errors to <data dir>/localflix.log (cleared when it grows
// past 1 MB) so problems can be diagnosed after the fact.
QFile* g_logFile = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const char* level = type == QtDebugMsg ? "DEBUG" : type == QtInfoMsg ? "INFO " : type == QtWarningMsg ? "WARN " : "ERROR";
    const QString line = QStringLiteral("%1 %2 [%3] %4\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), QString::fromLatin1(level), QString::fromLatin1(ctx.category), msg);
    if (g_logFile && g_logFile->isOpen()) {
        g_logFile->write(line.toUtf8());
        g_logFile->flush();
    }
    std::fputs(line.toLocal8Bit().constData(), stderr);
    if (type == QtFatalMsg) std::abort();
}

void installLogging(const QString& dataDir)
{
    QDir().mkpath(dataDir);
    const QString path = QDir(dataDir).filePath(QStringLiteral("localflix.log"));
    if (QFileInfo(path).size() > 1024 * 1024) QFile::remove(path);
    static QFile file(path);
    if (file.open(QIODevice::Append | QIODevice::Text)) g_logFile = &file;
    // info-level engine chatter is off unless the user asks for it with QT_LOGGING_RULES
    QLoggingCategory::setFilterRules(QStringLiteral("localflix.*.info=false\nlocalflix.*.debug=false"));
    qInstallMessageHandler(messageHandler);
}

}  // namespace

int main(int argc, char* argv[])
{
    // Troubleshooting: `--software-opengl` renders with Qt's bundled software OpenGL
    // (slow, but works when a GPU driver cannot present OpenGL windows).
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--software-opengl") == 0) QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);

    // The GL format must be chosen before QApplication exists: libmpv's renderer needs 3.x.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("LocalFlix"));
    QCoreApplication::setOrganizationName(QString());  // data lives in %APPDATA%\LocalFlix
    QCoreApplication::setApplicationVersion(QStringLiteral(LOCALFLIX_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/localflix.png")));
    lf::ui::Theme::apply(app);

    const QString dataDir = lf::ApplicationController::defaultDataDir();
    installLogging(dataDir);

    QString error;
    if (!lf::MpvBackend::libraryAvailable(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("LocalFlix"),
                              QStringLiteral("The video engine could not be loaded.\n\n%1\n\nReinstall LocalFlix or copy libmpv-2.dll next to LocalFlix.exe.").arg(error));
        return 2;
    }

    auto backend = std::make_unique<lf::MpvBackend>(lf::MpvBackend::Options{});
    if (!backend->isValid()) {
        QMessageBox::critical(nullptr, QStringLiteral("LocalFlix"), QStringLiteral("The video engine failed to start.\n\n%1").arg(backend->initError()));
        return 3;
    }
    lf::MpvBackend* engine = backend.get();

    // The controller owns the backend, so it is declared first and destroyed last.
    lf::ApplicationController controller(dataDir, std::move(backend));
    if (!controller.initialize(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("LocalFlix cannot open its library"),
                              QStringLiteral("%1\n\nData folder: %2").arg(error, QDir::toNativeSeparators(dataDir)));
        return 4;
    }

    int exitCode = 0;
    {
        // The window (and the video widget's libmpv render context) must die
        // before the controller tears down the engine.
        lf::ui::MainWindow window(controller, engine);
        window.show();
        // `--screenshot <png>`: render the window with Qt itself, then exit (docs / smoke test).
        const QStringList args = app.arguments();
        if (const int i = int(args.indexOf(QStringLiteral("--screenshot"))); i >= 0 && i + 1 < args.size()) {
            const QString target = args.at(i + 1);
            QTimer::singleShot(1500, &app, [&window, &app, target] {
                window.grab().save(target);
                app.quit();
            });
        }
        exitCode = app.exec();
    }
    controller.shutdown();
    return exitCode;
}
