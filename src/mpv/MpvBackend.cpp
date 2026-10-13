#include "MpvBackend.h"

#include <mpv/client.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(lcMpv, "localflix.mpv")

namespace lf {

namespace {

// reply_userdata ids for observed properties
enum Observed : uint64_t { ObsPosition = 1, ObsDuration, ObsPaused, ObsVolume, ObsMute, ObsSpeed, ObsTrackList, ObsAid, ObsSid };

// Frees the contents of an mpv_node on scope exit.
struct NodeGuard {
    mpv_node node{};
    ~NodeGuard() { mpv_free_node_contents(&node); }
};

const mpv_node* mapValue(const mpv_node& map, const char* key)
{
    if (map.format != MPV_FORMAT_NODE_MAP) return nullptr;
    for (int i = 0; i < map.u.list->num; ++i)
        if (std::strcmp(map.u.list->keys[i], key) == 0) return &map.u.list->values[i];
    return nullptr;
}

QString nodeString(const mpv_node& map, const char* key)
{
    const mpv_node* v = mapValue(map, key);
    return v && v->format == MPV_FORMAT_STRING ? QString::fromUtf8(v->u.string) : QString();
}

bool nodeFlag(const mpv_node& map, const char* key)
{
    const mpv_node* v = mapValue(map, key);
    return v && v->format == MPV_FORMAT_FLAG && v->u.flag != 0;
}

qint64 nodeInt(const mpv_node& map, const char* key, qint64 fallback = 0)
{
    const mpv_node* v = mapValue(map, key);
    if (!v) return fallback;
    if (v->format == MPV_FORMAT_INT64) return v->u.int64;
    if (v->format == MPV_FORMAT_DOUBLE) return qint64(v->u.double_);
    return fallback;
}

EndReason mapReason(mpv_end_file_reason r)
{
    switch (r) {
    case MPV_END_FILE_REASON_EOF: return EndReason::Eof;
    case MPV_END_FILE_REASON_STOP: return EndReason::Stop;
    case MPV_END_FILE_REASON_QUIT: return EndReason::Quit;
    case MPV_END_FILE_REASON_ERROR: return EndReason::Error;
    case MPV_END_FILE_REASON_REDIRECT: return EndReason::Redirect;
    }
    return EndReason::Stop;
}

QString friendlyError(int code)
{
    switch (code) {
    case MPV_ERROR_UNKNOWN_FORMAT: return QStringLiteral("unsupported or unrecognised file format");
    case MPV_ERROR_LOADING_FAILED: return QStringLiteral("the file could not be opened (missing, locked or corrupt)");
    case MPV_ERROR_NOTHING_TO_PLAY: return QStringLiteral("the file contains nothing that can be played");
    case MPV_ERROR_VO_INIT_FAILED: return QStringLiteral("the video output could not be initialised");
    case MPV_ERROR_AO_INIT_FAILED: return QStringLiteral("the audio output could not be initialised");
    default: break;
    }
    return QString::fromUtf8(mpv_error_string(code));
}

}  // namespace

bool MpvBackend::libraryAvailable(QString* error)
{
#ifdef _WIN32
    // Mirror the loader's behaviour for delay-loaded imports: application directory first.
    HMODULE h = ::LoadLibraryExW(L"libmpv-2.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) h = ::LoadLibraryW(L"libmpv-2.dll");
    if (!h) {
        if (error)
            *error = QStringLiteral("libmpv-2.dll was not found next to the application (Windows error %1).").arg(::GetLastError());
        return false;
    }
    ::FreeLibrary(h);
#endif
    Q_UNUSED(error)
    return true;
}

MpvBackend::MpvBackend(const Options& options, QObject* parent) : IPlaybackBackend(parent), m_options(options)
{
    qRegisterMetaType<lf::EndFileInfo>("lf::EndFileInfo");
    qRegisterMetaType<QVector<lf::TrackInfo>>("QVector<lf::TrackInfo>");

    m_handle = mpv_create();
    if (!m_handle) {
        m_initError = QStringLiteral("mpv_create failed");
        return;
    }

    // Everything the application controls itself is switched off in libmpv:
    // no config files, OSC, key bindings, terminal or youtube-dl hook.
    setOptionString("config", QStringLiteral("no"));
    setOptionString("terminal", QStringLiteral("no"));
    setOptionString("idle", QStringLiteral("yes"));          // keep the core alive between files
    setOptionString("keep-open", QStringLiteral("no"));      // END_FILE(eof) must fire at the end
    setOptionString("ytdl", QStringLiteral("no"));
    setOptionString("osc", QStringLiteral("no"));
    setOptionString("osd-level", QStringLiteral("0"));
    setOptionString("input-default-bindings", QStringLiteral("no"));
    setOptionString("input-vo-keyboard", QStringLiteral("no"));
    setOptionString("input-media-keys", QStringLiteral("no"));
    setOptionString("resume-playback", QStringLiteral("no"));  // LocalFlix owns resume positions
    setOptionString("save-position-on-quit", QStringLiteral("no"));
    setOptionString("volume-max", QStringLiteral("130"));
    setOptionString("sub-auto", QStringLiteral("fuzzy"));      // pick up .srt/.ass next to the video
    setOptionString("audio-display", QStringLiteral("no"));
    setOptionString("force-seekable", QStringLiteral("yes"));

    if (options.video) {
        setOptionString("vo", QStringLiteral("libmpv"));
        setOptionString("hwdec", options.hwdec);
        // MpvWidget renders with BLOCK_FOR_TARGET_TIME=0 (never stall the GUI thread), so libmpv
        // must not schedule frames ahead of the display either, or A/V sync drifts.
        setOptionString("video-timing-offset", QStringLiteral("0"));
    } else {
        setOptionString("vo", QStringLiteral("null"));
        setOptionString("hwdec", QStringLiteral("no"));
    }
    if (!options.audio) setOptionString("ao", QStringLiteral("null"));

    const int rc = mpv_initialize(m_handle);
    if (rc < 0) {
        m_initError = QStringLiteral("mpv_initialize failed: %1").arg(QString::fromUtf8(mpv_error_string(rc)));
        mpv_terminate_destroy(m_handle);
        m_handle = nullptr;
        return;
    }

    mpv_request_log_messages(m_handle, "warn");
    mpv_observe_property(m_handle, ObsPosition, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_handle, ObsDuration, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_handle, ObsPaused, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_handle, ObsVolume, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_handle, ObsMute, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_handle, ObsSpeed, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_handle, ObsTrackList, "track-list", MPV_FORMAT_NONE);
    mpv_observe_property(m_handle, ObsAid, "aid", MPV_FORMAT_NONE);
    mpv_observe_property(m_handle, ObsSid, "sid", MPV_FORMAT_NONE);

    mpv_set_wakeup_callback(m_handle, &MpvBackend::wakeupCallback, this);
}

MpvBackend::~MpvBackend()
{
    emit aboutToDestroy();  // lets MpvWidget free its render context first
    if (m_handle) {
        // After this returns libmpv will not invoke the callback again.
        mpv_set_wakeup_callback(m_handle, nullptr, nullptr);
        mpv_terminate_destroy(m_handle);
        m_handle = nullptr;
    }
}

QString MpvBackend::mpvVersion() const
{
    if (!m_handle) return {};
    char* v = mpv_get_property_string(m_handle, "mpv-version");
    const QString s = v ? QString::fromUtf8(v) : QString();
    if (v) mpv_free(v);
    return s;
}

void MpvBackend::wakeupCallback(void* ctx)
{
    // Arbitrary thread: only schedule a drain on the GUI thread (coalesced).
    auto* self = static_cast<MpvBackend*>(ctx);
    if (!self->m_wakeupPending.exchange(true))
        QMetaObject::invokeMethod(self, "processEvents", Qt::QueuedConnection);
}

bool MpvBackend::setOptionString(const char* name, const QString& value)
{
    const int rc = mpv_set_option_string(m_handle, name, value.toUtf8().constData());
    if (rc < 0) qCWarning(lcMpv) << "option" << name << "=" << value << "failed:" << mpv_error_string(rc);
    return rc >= 0;
}

bool MpvBackend::setPropertyString(const char* name, const QString& value)
{
    if (!m_handle) return false;
    const int rc = mpv_set_property_string(m_handle, name, value.toUtf8().constData());
    if (rc < 0) qCWarning(lcMpv) << "property" << name << "=" << value << "failed:" << mpv_error_string(rc);
    return rc >= 0;
}

int MpvBackend::command(const QByteArrayList& args)
{
    if (!m_handle) return MPV_ERROR_UNINITIALIZED;
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const QByteArray& a : args) argv.push_back(a.constData());
    argv.push_back(nullptr);
    return mpv_command(m_handle, argv.data());
}

// ------------------------------------------------------------------ commands

quint64 MpvBackend::loadFile(const QString& path, double startSeconds)
{
    const quint64 token = m_nextToken++;
    m_lastLoadToken = token;
    if (!m_handle) return token;

    // A new file always starts playing, even if the previous one was left paused.
    setPropertyString("pause", QStringLiteral("no"));
    m_position = 0.0;
    m_lastEmittedPosition = -1.0;
    m_duration = 0.0;

    const QByteArray utf8 = QDir::toNativeSeparators(path).toUtf8();
    const QByteArray startOpt = startSeconds > 0.5 ? QByteArray("start=") + QByteArray::number(startSeconds, 'f', 3) : QByteArray("start=0");

    // mpv >= 0.38: loadfile <url> <flags> <index> <options>
    mpv_node result{};
    const char* args[] = {"loadfile", utf8.constData(), "replace", "-1", startOpt.constData(), nullptr};
    int rc = mpv_command_ret(m_handle, args, &result);
    if (rc < 0) {
        // Older libmpv: loadfile <url> <flags> <options>
        const char* legacy[] = {"loadfile", utf8.constData(), "replace", startOpt.constData(), nullptr};
        rc = mpv_command_ret(m_handle, legacy, &result);
    }
    if (rc >= 0) {
        if (result.format == MPV_FORMAT_NODE_MAP) {
            const qint64 entryId = nodeInt(result, "playlist_entry_id", 0);
            if (entryId > 0) m_entryTokens.insert(entryId, token);
        }
        mpv_free_node_contents(&result);
    } else {
        qCWarning(lcMpv) << "loadfile failed:" << mpv_error_string(rc);
        // Surface it as a failed playback so the controller can react uniformly.
        EndFileInfo info;
        info.token = token;
        info.reason = EndReason::Error;
        info.errorCode = rc;
        info.errorText = friendlyError(rc);
        QMetaObject::invokeMethod(this, [this, info] { emit endFile(info); }, Qt::QueuedConnection);
    }
    return token;
}

void MpvBackend::stop()
{
    command({"stop"});
}

void MpvBackend::setPaused(bool paused)
{
    setPropertyString("pause", paused ? QStringLiteral("yes") : QStringLiteral("no"));
}

void MpvBackend::seekAbsolute(double seconds)
{
    command({"seek", QByteArray::number(std::max(0.0, seconds), 'f', 3), "absolute"});
}

void MpvBackend::seekRelative(double seconds)
{
    command({"seek", QByteArray::number(seconds, 'f', 3), "relative"});
}

void MpvBackend::setVolume(int percent)
{
    m_volume = std::clamp(percent, 0, 130);
    setPropertyString("volume", QString::number(m_volume));
}

void MpvBackend::setMuted(bool muted)
{
    setPropertyString("mute", muted ? QStringLiteral("yes") : QStringLiteral("no"));
}

void MpvBackend::setSpeed(double speed)
{
    setPropertyString("speed", QString::number(std::clamp(speed, 0.25, 4.0), 'f', 2));
}

void MpvBackend::setAudioTrack(int trackId)
{
    setPropertyString("aid", trackId <= 0 ? QStringLiteral("no") : QString::number(trackId));
}

void MpvBackend::setSubtitleTrack(int trackId)
{
    setPropertyString("sid", trackId <= 0 ? QStringLiteral("no") : QString::number(trackId));
}

bool MpvBackend::addExternalSubtitle(const QString& path)
{
    if (!QFileInfo::exists(path)) return false;
    // "select" makes the new track the active one right away.
    return command({"sub-add", QDir::toNativeSeparators(path).toUtf8(), "select"}) >= 0;
}

void MpvBackend::setPreferredLanguages(const QString& audio, const QString& subtitle)
{
    auto normalise = [](const QString& s) {
        QStringList parts = s.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
        return parts.join(QLatin1Char(','));
    };
    setPropertyString("alang", normalise(audio));
    setPropertyString("slang", normalise(subtitle));
}

// ------------------------------------------------------------------ events

quint64 MpvBackend::tokenForEntry(qint64 entryId) const
{
    if (entryId > 0) {
        const auto it = m_entryTokens.constFind(entryId);
        if (it != m_entryTokens.constEnd()) return it.value();
    }
    return 0;
}

void MpvBackend::processEvents()
{
    m_wakeupPending = false;
    while (m_handle) {
        mpv_event* ev = mpv_wait_event(m_handle, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        handleEvent(ev);
        if (ev->event_id == MPV_EVENT_SHUTDOWN) break;
    }
}

void MpvBackend::handleEvent(const mpv_event* ev)
{
    switch (ev->event_id) {
    case MPV_EVENT_START_FILE: {
        const auto* sf = static_cast<const mpv_event_start_file*>(ev->data);
        quint64 token = tokenForEntry(sf->playlist_entry_id);
        if (token == 0) token = m_lastLoadToken;  // entry ids unavailable: newest load
        m_activeToken = token;
        m_position = 0.0;
        m_duration = 0.0;
        m_lastEmittedPosition = -1.0;
        break;
    }
    case MPV_EVENT_FILE_LOADED: {
        // The duration is usually known by now; read it directly rather than
        // waiting for the property-change notification.
        double d = 0.0;
        if (mpv_get_property(m_handle, "duration", MPV_FORMAT_DOUBLE, &d) >= 0 && d > 0) m_duration = d;
        emit fileLoaded(m_activeToken);
        refreshTracks();
        break;
    }
    case MPV_EVENT_END_FILE: {
        const auto* ef = static_cast<const mpv_event_end_file*>(ev->data);
        EndFileInfo info;
        info.token = tokenForEntry(ef->playlist_entry_id);
        if (info.token == 0) info.token = m_activeToken;
        info.reason = mapReason(ef->reason);
        info.errorCode = ef->error;
        if (ef->reason == MPV_END_FILE_REASON_ERROR) info.errorText = friendlyError(ef->error);
        m_entryTokens.remove(ef->playlist_entry_id);
        if (!m_tracks.isEmpty()) {
            m_tracks.clear();
            emit tracksChanged(m_tracks);
        }
        qCInfo(lcMpv) << "end-file token" << info.token << "reason" << int(info.reason) << info.errorText;
        emit endFile(info);
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE:
        handlePropertyChange(ev);
        break;
    case MPV_EVENT_LOG_MESSAGE: {
        const auto* msg = static_cast<const mpv_event_log_message*>(ev->data);
        qCWarning(lcMpv).noquote() << QStringLiteral("[%1] %2").arg(QString::fromUtf8(msg->prefix), QString::fromUtf8(msg->text).trimmed());
        break;
    }
    case MPV_EVENT_SHUTDOWN:
        qCInfo(lcMpv) << "mpv core shut down";
        break;
    default:
        break;
    }
}

void MpvBackend::handlePropertyChange(const mpv_event* ev)
{
    const auto* prop = static_cast<const mpv_event_property*>(ev->data);
    switch (ev->reply_userdata) {
    case ObsPosition:
        if (prop->format == MPV_FORMAT_DOUBLE) {
            m_position = *static_cast<double*>(prop->data);
            // Rate-limit: ~4 updates per second, but always report backward jumps (seeks).
            if (m_lastEmittedPosition < 0 || std::fabs(m_position - m_lastEmittedPosition) >= 0.25 || m_position < m_lastEmittedPosition) {
                m_lastEmittedPosition = m_position;
                emit positionChanged(m_position);
            }
        }
        break;
    case ObsDuration:
        if (prop->format == MPV_FORMAT_DOUBLE) {
            m_duration = *static_cast<double*>(prop->data);
            emit durationChanged(m_duration);
        } else {
            m_duration = 0.0;
        }
        break;
    case ObsPaused:
        if (prop->format == MPV_FORMAT_FLAG) {
            m_paused = *static_cast<int*>(prop->data) != 0;
            emit pausedChanged(m_paused);
        }
        break;
    case ObsVolume:
        if (prop->format == MPV_FORMAT_DOUBLE) {
            m_volume = int(std::lround(*static_cast<double*>(prop->data)));
            emit volumeChanged(m_volume);
        }
        break;
    case ObsMute:
        if (prop->format == MPV_FORMAT_FLAG) {
            m_muted = *static_cast<int*>(prop->data) != 0;
            emit mutedChanged(m_muted);
        }
        break;
    case ObsSpeed:
        if (prop->format == MPV_FORMAT_DOUBLE) {
            m_speed = *static_cast<double*>(prop->data);
            emit speedChanged(m_speed);
        }
        break;
    case ObsTrackList:
    case ObsAid:
    case ObsSid:
        refreshTracks();
        break;
    default:
        break;
    }
}

void MpvBackend::refreshTracks()
{
    if (!m_handle) return;
    NodeGuard list;
    if (mpv_get_property(m_handle, "track-list", MPV_FORMAT_NODE, &list.node) < 0 || list.node.format != MPV_FORMAT_NODE_ARRAY) return;

    QVector<TrackInfo> tracks;
    for (int i = 0; i < list.node.u.list->num; ++i) {
        const mpv_node& t = list.node.u.list->values[i];
        const QString type = nodeString(t, "type");
        TrackInfo info;
        if (type == QLatin1String("audio"))
            info.type = TrackType::Audio;
        else if (type == QLatin1String("sub"))
            info.type = TrackType::Subtitle;
        else
            continue;  // video tracks are not user-selectable here
        info.id = int(nodeInt(t, "id"));
        info.language = nodeString(t, "lang");
        info.title = nodeString(t, "title");
        info.codec = nodeString(t, "codec");
        info.selected = nodeFlag(t, "selected");
        info.external = nodeFlag(t, "external");
        info.isDefault = nodeFlag(t, "default");
        tracks.push_back(info);
    }

    auto same = [](const QVector<TrackInfo>& a, const QVector<TrackInfo>& b) {
        if (a.size() != b.size()) return false;
        for (int i = 0; i < a.size(); ++i)
            if (a[i].id != b[i].id || a[i].type != b[i].type || a[i].selected != b[i].selected || a[i].title != b[i].title) return false;
        return true;
    };
    if (same(tracks, m_tracks)) return;
    m_tracks = tracks;
    emit tracksChanged(m_tracks);
}

}  // namespace lf
