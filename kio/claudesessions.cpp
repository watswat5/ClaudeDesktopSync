// claude-sessions:/ — a read-only KIO worker that shows the Claude desktop app's
// Code sessions as a folder tree mirroring their working directories.
//
// Reads ~/.config/Claude/claude-code-sessions/*/*/local_*.json on every request,
// so listings are always current. Never writes anything.

#include <KIO/WorkerBase>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QUrl>

#include <sys/stat.h>

class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.claude-sessions" FILE "claude-sessions.json")
};

namespace
{
const QString kNoWorkspace = QStringLiteral("No workspace");
const QString kCloud = QStringLiteral("Cloud");
const QString kArchived = QStringLiteral("Archived");
const QString kOutsideHome = QStringLiteral("Outside home");
const QString kCloudEntry = QStringLiteral("Cloud sessions on claude.ai");

struct Session {
    QString id;
    QString title;
    QString cwd;
    QString summary;
    QDateTime lastActivity;
    bool archived = false;
    QStringList dir; // virtual folder, as path components
    QString name; // unique file name inside that folder
};

QString appDir()
{
    return QDir::homePath() + QStringLiteral("/.config/Claude");
}

QStringList virtualDirFor(const QString &cwd)
{
    const QString home = QDir::homePath();
    const QString scratch = appDir() + QStringLiteral("/scratch-workspaces/");
    if (cwd.isEmpty() || cwd.startsWith(scratch)) {
        return {kNoWorkspace};
    }
    const QString clean = QDir::cleanPath(cwd);
    if (clean == home) {
        return {};
    }
    if (clean.startsWith(home + QLatin1Char('/'))) {
        return clean.mid(home.size() + 1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    }
    return QStringList{kOutsideHome} + clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QList<Session> loadSessions()
{
    QList<Session> sessions;
    QSet<QString> archivedIds;
    QDir root(appDir() + QStringLiteral("/claude-code-sessions"));
    for (const QString &account : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir accountDir(root.filePath(account));
        for (const QString &org : accountDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir orgDir(accountDir.filePath(org));
            QFile idx(orgDir.filePath(QStringLiteral("archived-sessions.idx")));
            if (idx.open(QIODevice::ReadOnly)) {
                for (const QJsonValue &v : QJsonDocument::fromJson(idx.readAll()).object().value(QLatin1String("archived")).toArray()) {
                    archivedIds.insert(v.toString());
                }
            }
            for (const QString &file : orgDir.entryList({QStringLiteral("local_*.json")}, QDir::Files)) {
                QFile f(orgDir.filePath(file));
                if (!f.open(QIODevice::ReadOnly)) {
                    continue;
                }
                const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
                if (o.isEmpty()) {
                    continue; // mid-write; the next listing picks it up
                }
                Session s;
                s.id = o.value(QLatin1String("sessionId")).toString();
                if (!s.id.startsWith(QLatin1String("local_"))) {
                    continue;
                }
                s.title = o.value(QLatin1String("title")).toString();
                if (s.title.trimmed().isEmpty()) {
                    s.title = QStringLiteral("Untitled");
                }
                s.cwd = o.value(QLatin1String("cwd")).toString();
                s.summary = o.value(QLatin1String("postTurnSummary")).toObject().value(QLatin1String("status_detail")).toString();
                double ts = o.value(QLatin1String("lastActivityAt")).toDouble();
                if (ts <= 0) {
                    ts = o.value(QLatin1String("createdAt")).toDouble();
                }
                s.lastActivity = QDateTime::fromMSecsSinceEpoch(qint64(ts));
                s.archived = o.value(QLatin1String("isArchived")).toBool();
                sessions.append(s);
            }
        }
    }
    for (Session &s : sessions) {
        s.archived = s.archived || archivedIds.contains(s.id);
        s.dir = s.archived ? QStringList{kArchived} : virtualDirFor(s.cwd);
        QString title = s.title.simplified();
        title.replace(QLatin1Char('/'), QLatin1Char(' '));
        s.name = QStringLiteral("%1 [%2]").arg(title.left(80), s.id.mid(6, 8));
    }
    return sessions;
}

QStringList splitPath(const QUrl &url)
{
    return url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool startsWith(const QStringList &path, const QStringList &prefix)
{
    if (prefix.size() > path.size()) {
        return false;
    }
    for (int i = 0; i < prefix.size(); ++i) {
        if (path.at(i) != prefix.at(i)) {
            return false;
        }
    }
    return true;
}

KIO::UDSEntry dirEntry(const QString &name, const QString &icon = QStringLiteral("folder"), const QString &comment = {})
{
    KIO::UDSEntry e;
    e.fastInsert(KIO::UDSEntry::UDS_NAME, name);
    e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0555);
    e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, icon);
    if (!comment.isEmpty()) {
        e.fastInsert(KIO::UDSEntry::UDS_COMMENT, comment);
    }
    return e;
}

QString deepLink(const Session &s)
{
    return QStringLiteral("claude://claude.ai/epitaxy/") + s.id;
}

KIO::UDSEntry sessionEntry(const Session &s)
{
    KIO::UDSEntry e;
    e.fastInsert(KIO::UDSEntry::UDS_NAME, s.name);
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, s.title);
    e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
    e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0444);
    e.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
    e.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, s.lastActivity.toSecsSinceEpoch());
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_TYPE, s.archived ? QStringLiteral("Claude session (archived)") : QStringLiteral("Claude session"));
    e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("claude-desktop"));
    // Dolphin opens an item's target URL. Point it at the executable launcher
    // claude_sessions_sync.py keeps per session, which Dolphin runs like the
    // symlinks in project folders. A claude:// target makes Dolphin ask which
    // app to use instead.
    const QString launcher = QDir::homePath() + QStringLiteral("/.local/share/claude-dolphin/sessions/") + s.id + QStringLiteral(".desktop");
    if (QFile::exists(launcher)) {
        e.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, QUrl::fromLocalFile(launcher).toString());
        e.fastInsert(KIO::UDSEntry::UDS_LOCAL_PATH, launcher);
        e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("application/x-desktop"));
    } else {
        e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("x-scheme-handler/claude"));
        e.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, deepLink(s));
    }
    QString comment = s.cwd;
    if (!s.summary.isEmpty()) {
        comment += QStringLiteral(" — ") + s.summary;
    }
    e.fastInsert(KIO::UDSEntry::UDS_COMMENT, comment);
    return e;
}

KIO::UDSEntry cloudEntry()
{
    KIO::UDSEntry e;
    e.fastInsert(KIO::UDSEntry::UDS_NAME, kCloudEntry);
    e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
    e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0444);
    e.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
    e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("x-scheme-handler/https"));
    e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-cloud"));
    e.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, QStringLiteral("https://claude.ai/code"));
    e.fastInsert(KIO::UDSEntry::UDS_COMMENT, QStringLiteral("Cloud runs are not stored on this computer; this opens them on claude.ai"));
    return e;
}

QString iconFor(const QStringList &path)
{
    if (path.size() == 1) {
        if (path.first() == kCloud) {
            return QStringLiteral("folder-cloud");
        }
        if (path.first() == kArchived) {
            return QStringLiteral("folder-archive");
        }
        if (path.first() == kNoWorkspace) {
            return QStringLiteral("folder-temp");
        }
    }
    return QStringLiteral("folder");
}

// The tree as seen from one folder.
struct Listing {
    bool exists = false;
    QMap<QString, int> subdirs; // name -> sessions below (for the tooltip)
    QList<Session> sessions;
};

Listing listingFor(const QStringList &path, const QList<Session> &all)
{
    Listing l;
    if (path.isEmpty()) {
        l.exists = true;
        l.subdirs.insert(kCloud, 0);
        l.subdirs.insert(kNoWorkspace, 0);
        l.subdirs.insert(kArchived, 0);
    } else if (path == QStringList{kCloud} || path == QStringList{kNoWorkspace} || path == QStringList{kArchived}) {
        l.exists = true;
    }
    for (const Session &s : all) {
        if (!startsWith(s.dir, path)) {
            continue;
        }
        l.exists = true;
        if (s.dir.size() == path.size()) {
            l.sessions.append(s);
        } else {
            l.subdirs[s.dir.at(path.size())] += 1;
        }
    }
    return l;
}

QString sessionsTooltip(int n)
{
    return n == 1 ? QStringLiteral("1 Claude session") : QStringLiteral("%1 Claude sessions").arg(n);
}
} // namespace

class ClaudeSessionsWorker : public KIO::WorkerBase
{
public:
    ClaudeSessionsWorker(const QByteArray &pool, const QByteArray &app)
        : KIO::WorkerBase("claude-sessions", pool, app)
    {
    }

    KIO::WorkerResult listDir(const QUrl &url) override
    {
        const QStringList path = splitPath(url);
        const QList<Session> all = loadSessions();
        const Listing l = listingFor(path, all);
        if (!l.exists) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        }
        KIO::UDSEntryList entries;
        entries.append(dirEntry(QStringLiteral(".")));
        for (auto it = l.subdirs.cbegin(); it != l.subdirs.cend(); ++it) {
            entries.append(dirEntry(it.key(), iconFor(path + QStringList{it.key()}), it.value() ? sessionsTooltip(it.value()) : QString()));
        }
        for (const Session &s : l.sessions) {
            entries.append(sessionEntry(s));
        }
        if (path == QStringList{kCloud}) {
            entries.append(cloudEntry());
        }
        listEntries(entries);
        return KIO::WorkerResult::pass();
    }

    KIO::WorkerResult stat(const QUrl &url) override
    {
        const QStringList path = splitPath(url);
        if (path.isEmpty()) {
            KIO::UDSEntry e = dirEntry(QStringLiteral("."), QStringLiteral("claude-desktop"));
            e.replace(KIO::UDSEntry::UDS_DISPLAY_NAME, QStringLiteral("Claude Sessions"));
            statEntry(e);
            return KIO::WorkerResult::pass();
        }
        const QList<Session> all = loadSessions();
        const QStringList parent = path.mid(0, path.size() - 1);
        const QString leaf = path.last();
        if (path == QStringList{kCloud, kCloudEntry}) {
            statEntry(cloudEntry());
            return KIO::WorkerResult::pass();
        }
        for (const Session &s : all) {
            if (s.dir == parent && s.name == leaf) {
                statEntry(sessionEntry(s));
                return KIO::WorkerResult::pass();
            }
        }
        if (listingFor(path, all).exists) {
            statEntry(dirEntry(leaf, iconFor(path)));
            return KIO::WorkerResult::pass();
        }
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    }

    // Reading a session (e.g. copying it out) yields a .desktop link to it.
    KIO::WorkerResult get(const QUrl &url) override
    {
        const QStringList path = splitPath(url);
        if (path.isEmpty()) {
            return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString());
        }
        const QStringList parent = path.mid(0, path.size() - 1);
        for (const Session &s : loadSessions()) {
            if (s.dir == parent && s.name == path.last()) {
                const QByteArray content = QStringLiteral("[Desktop Entry]\nType=Link\nName=%1\nIcon=claude-desktop\nURL=%2\n")
                                            .arg(s.title, deepLink(s))
                                            .toUtf8();
                mimeType(QStringLiteral("application/x-desktop"));
                totalSize(content.size());
                data(content);
                data(QByteArray());
                return KIO::WorkerResult::pass();
            }
        }
        if (listingFor(path, loadSessions()).exists) {
            return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString());
        }
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    }
};

extern "C" Q_DECL_EXPORT int kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_claude_sessions"));
    if (argc != 4) {
        fprintf(stderr, "Usage: kio_claude_sessions protocol domain-socket1 domain-socket2\n");
        return -1;
    }
    ClaudeSessionsWorker worker(argv[2], argv[3]);
    worker.dispatchLoop();
    return 0;
}

#include "claudesessions.moc"
