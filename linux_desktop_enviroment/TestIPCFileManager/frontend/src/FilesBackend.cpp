#include "FilesBackend.h"

#include <kj/async.h>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QIcon>
#include <QLocale>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QPalette>
#include <QProcess>
#include <QQmlEngine>
#include <QUrl>

namespace {

FilesBackend *s_instance = nullptr;

const QString StartPage = QStringLiteral("start");
const QString ComputerPage = QStringLiteral("computer");

QVariantMap entryToMap(const FileEntry &entry)
{
    return {
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("path"), QDir(entry.location).filePath(entry.name)},
        {QStringLiteral("location"), entry.location},
        {QStringLiteral("isDir"), entry.isDir},
        {QStringLiteral("size"), double(entry.size)},
        {QStringLiteral("modified"), double(entry.modified)},
        {QStringLiteral("mimeType"), entry.mimeType},
        {QStringLiteral("iconName"), entry.iconName},
        {QStringLiteral("typeName"), entry.typeName},
    };
}

} // namespace

FilesBackend::FilesBackend(const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_connection(new ServiceConnection(socketPath, this))
{
    s_instance = this;
    setDarkIcons(false);
    connect(m_connection, &ServiceConnection::connectedChanged, this, [this](bool connected) {
        emit connectedChanged();
        if (connected) {
            loadStartupData();
        } else if (m_currentJob != 0) {
            // Zadanie przerwane razem z połączeniem
            m_currentJob = 0;
            m_jobText.clear();
            emit jobChanged();
        }
    });
}

FilesBackend::~FilesBackend()
{
    // Najpierw zatrzymujemy wątek RPC, który może jeszcze odwoływać się do tego obiektu
    delete m_connection;
    m_connection = nullptr;
    s_instance = nullptr;
}

FilesBackend *FilesBackend::instance()
{
    return s_instance;
}

FilesBackend *FilesBackend::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

// --- Dane startowe ---

void FilesBackend::loadStartupData()
{
    ping();

    m_connection->post([this](KjContext &context) {
        context.tasks->add(context.service.placesRequest().send().then([this](auto &&response) {
            QVariantList places;
            QHash<QString, QString> icons;
            for (auto place : response.getPlaces()) {
                const QString path = toQString(place.getPath());
                const QString icon = toQString(place.getIconName());
                places.append(QVariantMap{
                    {QStringLiteral("name"), toQString(place.getName())},
                    {QStringLiteral("path"), path},
                    {QStringLiteral("iconName"), icon},
                });
                icons.insert(path, icon);
            }
            m_connection->toGui([this, places, icons] {
                m_places = places;
                for (auto it = icons.cbegin(); it != icons.cend(); ++it)
                    m_placeIcons.insert(it.key(), it.value());
                m_iconCache.clear();
                emit placesChanged();
                bumpRevision();
            });
        }));
    });

    refreshDrives();
    refreshRecent();
}

void FilesBackend::ping()
{
    m_connection->post([this](KjContext &context) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        context.tasks->add(context.service.infoRequest().send().then([this, timer](auto &&response) {
            const double ms = timer->nsecsElapsed() / 1e6;
            const QString home = toQString(response.getHomePath());
            const QString trash = toQString(response.getTrashPath());
            const QString version = toQString(response.getServiceVersion());
            m_connection->toGui([this, home, trash, version, ms] {
                m_homePath = home;
                m_trashPath = trash;
                m_serviceVersion = version;
                m_lastCallMs = ms;
                m_placeIcons.insert(trash, QStringLiteral("user-trash"));
                m_iconCache.clear();
                emit infoChanged();
                bumpRevision();
            });
        }));
    });
}

void FilesBackend::refreshDrives()
{
    m_connection->post([this](KjContext &context) {
        context.tasks->add(context.service.drivesRequest().send().then([this](auto &&response) {
            QVariantList drives;
            for (auto drive : response.getDrives()) {
                drives.append(QVariantMap{
                    {QStringLiteral("name"), toQString(drive.getName())},
                    {QStringLiteral("mountPoint"), toQString(drive.getMountPoint())},
                    {QStringLiteral("device"), toQString(drive.getDevice())},
                    {QStringLiteral("fsType"), toQString(drive.getFsType())},
                    {QStringLiteral("total"), double(drive.getTotal())},
                    {QStringLiteral("free"), double(drive.getFree())},
                    {QStringLiteral("removable"), drive.getRemovable()},
                });
            }
            m_connection->toGui([this, drives] {
                m_drives = drives;
                for (const QVariant &drive : drives) {
                    const QVariantMap map = drive.toMap();
                    m_placeIcons.insert(map.value(QStringLiteral("mountPoint")).toString(),
                                        map.value(QStringLiteral("removable")).toBool()
                                            ? QStringLiteral("drive-removable-media")
                                            : QStringLiteral("drive-harddisk"));
                }
                m_iconCache.clear();
                emit drivesChanged();
                bumpRevision();
            });
        }));
    });
}

void FilesBackend::refreshRecent()
{
    m_connection->post([this](KjContext &context) {
        auto request = context.service.recentRequest();
        request.setLimit(12);
        context.tasks->add(request.send().then([this](auto &&response) {
            QVector<FileEntry> entries;
            for (auto entry : response.getEntries())
                entries.append(toFileEntry(entry));
            m_connection->toGui([this, entries] {
                QVariantList recent;
                for (FileEntry entry : entries) {
                    entry.iconName = themeIcon(entry.iconName, entry.mimeType, entry.isDir);
                    recent.append(entryToMap(entry));
                }
                m_recent = recent;
                emit recentChanged();
            });
        }));
    });
}

// Paleta aplikacji według motywu okna: z niej silnik ikon KDE bierze kolor tekstu i akcentu
// w ikonach Breeze, więc ikony pasują do tła niezależnie od schematu kolorów użytkownika
void FilesBackend::setDarkIcons(bool dark)
{
    const QColor background = dark ? QColor(0x23, 0x23, 0x26) : QColor(0xfc, 0xfc, 0xfc);
    const QColor text = dark ? QColor(0xfc, 0xfc, 0xfc) : QColor(0x23, 0x26, 0x29);
    QPalette palette;
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::Base, background);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
    palette.setColor(QPalette::Accent, QColor(0x3d, 0xae, 0xe9));
    QGuiApplication::setPalette(palette);
    if (m_darkIcons == dark)
        return;
    m_darkIcons = dark;
    emit darkIconsChanged();
}

void FilesBackend::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
}

// --- Wywołania zwrotne QML ---

quint64 FilesBackend::storeCallback(const QJSValue &callback)
{
    const quint64 id = m_nextCallback++;
    m_callbacks.insert(id, callback);
    return id;
}

void FilesBackend::invokeCallback(quint64 id, const QJSValueList &arguments)
{
    QJSValue callback = m_callbacks.take(id);
    if (callback.isCallable())
        callback.call(arguments);
}

// --- Operacje ---

void FilesBackend::createFolder(const QString &parent, const QString &baseName, const QJSValue &callback)
{
    const quint64 id = storeCallback(callback);
    if (!connected()) {
        invokeCallback(id, {QString(), tr("The file service is not running")});
        return;
    }
    const QByteArray utf8Parent = parent.toUtf8();
    const QByteArray utf8Name = baseName.toUtf8();
    m_connection->post([this, id, utf8Parent, utf8Name](KjContext &context) {
        auto request = context.service.createFolderRequest();
        request.setParent(utf8Parent.constData());
        request.setBaseName(utf8Name.constData());
        context.tasks->add(request.send().then(
            [this, id](auto &&response) {
                const QString name = toQString(response.getName());
                m_connection->toGui([this, id, name] { invokeCallback(id, {name, QString()}); });
            },
            [this, id](kj::Exception &&exception) {
                const QString message = QString::fromUtf8(exception.getDescription().cStr());
                m_connection->toGui([this, id, message] { invokeCallback(id, {QString(), message}); });
            }));
    });
}

void FilesBackend::rename(const QString &parent, const QString &oldName, const QString &newName, const QJSValue &callback)
{
    const quint64 id = storeCallback(callback);
    if (!connected()) {
        invokeCallback(id, {false, tr("The file service is not running")});
        return;
    }
    const QByteArray utf8Parent = parent.toUtf8();
    const QByteArray utf8Old = oldName.toUtf8();
    const QByteArray utf8New = newName.toUtf8();
    m_connection->post([this, id, utf8Parent, utf8Old, utf8New](KjContext &context) {
        auto request = context.service.renameRequest();
        request.setParent(utf8Parent.constData());
        request.setOldName(utf8Old.constData());
        request.setNewName(utf8New.constData());
        context.tasks->add(request.send().then(
            [this, id](auto &&response) {
                const bool ok = response.getOk();
                const QString message = toQString(response.getMessage());
                m_connection->toGui([this, id, ok, message] { invokeCallback(id, {ok, message}); });
            },
            [this, id](kj::Exception &&exception) {
                const QString message = QString::fromUtf8(exception.getDescription().cStr());
                m_connection->toGui([this, id, message] { invokeCallback(id, {false, message}); });
            }));
    });
}

void FilesBackend::startJob(quint64 callbackId, const QString &text)
{
    m_jobText = text;
    m_jobProgress = 0;
    emit jobChanged();
    Q_UNUSED(callbackId)
}

void FilesBackend::handleJobEvent(quint64 callbackId, const JobEvent &event)
{
    if (event.finished) {
        m_currentJob = 0;
        m_jobText.clear();
        m_jobProgress = 0;
        emit jobChanged();
        invokeCallback(callbackId, {event.ok, event.message});
        return;
    }
    if (event.totalBytes > 0)
        m_jobProgress = double(event.doneBytes) / double(event.totalBytes);
    else if (event.totalItems > 0)
        m_jobProgress = double(event.doneItems) / double(event.totalItems);
    if (!event.current.isEmpty())
        m_jobText = event.current;
    emit jobChanged();
}

void FilesBackend::trash(const QStringList &paths, const QJSValue &callback)
{
    const quint64 callbackId = storeCallback(callback);
    if (!connected()) {
        invokeCallback(callbackId, {false, tr("The file service is not running")});
        return;
    }
    startJob(callbackId, tr("Moving to the Recycle Bin…"));
    m_currentJob = m_connection->trash(paths, [this, callbackId](const JobEvent &event) {
        handleJobEvent(callbackId, event);
    });
    emit jobChanged();
}

void FilesBackend::transfer(const QStringList &sources, const QString &destination, bool move, const QJSValue &callback)
{
    const quint64 callbackId = storeCallback(callback);
    if (!connected()) {
        invokeCallback(callbackId, {false, tr("The file service is not running")});
        return;
    }
    startJob(callbackId, move ? tr("Moving…") : tr("Copying…"));
    m_currentJob = m_connection->transfer(sources, destination, move, [this, callbackId](const JobEvent &event) {
        handleJobEvent(callbackId, event);
    });
    emit jobChanged();
}

void FilesBackend::cancelJob()
{
    // Porzucenie uchwytu zadania przerywa je w usłudze, która potem zgłasza finished(ok = false)
    if (m_currentJob != 0)
        m_connection->cancelJob(m_currentJob);
}

// --- Ścieżki ---

bool FilesBackend::isFolderPath(const QString &path) const
{
    return path.startsWith(QLatin1Char('/'));
}

bool FilesBackend::isInTrash(const QString &path) const
{
    return !m_trashPath.isEmpty() && (path == m_trashPath || path.startsWith(m_trashPath + QLatin1Char('/')));
}

QString FilesBackend::displayName(const QString &path) const
{
    if (path == StartPage)
        return tr("Home");
    if (path == ComputerPage)
        return tr("This PC");
    if (path == m_trashPath)
        return tr("Recycle Bin");
    if (path == QLatin1String("/"))
        return tr("File System");
    for (const QVariant &drive : m_drives) {
        const QVariantMap map = drive.toMap();
        if (map.value(QStringLiteral("mountPoint")).toString() == path)
            return map.value(QStringLiteral("name")).toString();
    }
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QString FilesBackend::parentPath(const QString &path) const
{
    if (!isFolderPath(path))
        return {};
    if (path == QLatin1String("/"))
        return ComputerPage;
    if (path == m_trashPath)
        return StartPage;
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

QString FilesBackend::joinPath(const QString &folder, const QString &name) const
{
    return folder.endsWith(QLatin1Char('/')) ? folder + name : folder + QLatin1Char('/') + name;
}

QVariantList FilesBackend::segments(const QString &path) const
{
    auto segment = [this](const QString &p) {
        return QVariantMap{{QStringLiteral("name"), displayName(p)}, {QStringLiteral("path"), p}};
    };
    if (!isFolderPath(path))
        return {segment(path)};

    // Najdłuższy znany korzeń: kosz, katalog domowy, punkt montowania albo "/"
    QString root = QStringLiteral("/");
    QStringList roots{m_trashPath, m_homePath};
    for (const QVariant &drive : m_drives)
        roots.append(drive.toMap().value(QStringLiteral("mountPoint")).toString());
    for (const QString &candidate : roots) {
        if (candidate.isEmpty() || candidate == QLatin1String("/"))
            continue;
        if ((path == candidate || path.startsWith(candidate + QLatin1Char('/'))) && candidate.size() > root.size())
            root = candidate;
    }

    QVariantList result;
    if (root == QLatin1String("/"))
        result.append(segment(ComputerPage));
    result.append(segment(root));
    QString current = root;
    const QStringList rest = path.mid(root.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : rest) {
        current = joinPath(current, part);
        result.append(segment(current));
    }
    return result;
}

QString FilesBackend::toLocation(const QString &path) const
{
    return isFolderPath(path) ? path : displayName(path);
}

QString FilesBackend::fromLocation(const QString &text) const
{
    QString location = text.trimmed();
    const QString lower = location.toLower();
    if (lower == QLatin1String("home") || lower == tr("Home").toLower())
        return StartPage;
    if (lower == QLatin1String("this pc") || lower == tr("This PC").toLower())
        return ComputerPage;
    if (lower == QLatin1String("trash") || lower == QLatin1String("recycle bin") || lower == tr("Recycle Bin").toLower())
        return m_trashPath;
    if (location == QLatin1String("~"))
        return m_homePath;
    if (location.startsWith(QLatin1String("~/")))
        location = m_homePath + location.mid(1);
    if (location.startsWith(QLatin1String("file://")))
        location = QUrl(location).toLocalFile();
    if (!location.startsWith(QLatin1Char('/')))
        return {};
    return QDir::cleanPath(location);
}

QString FilesBackend::shortPath(const QString &path) const
{
    if (!m_homePath.isEmpty() && (path == m_homePath || path.startsWith(m_homePath + QLatin1Char('/'))))
        return QLatin1Char('~') + path.mid(m_homePath.size());
    return path;
}

QStringList FilesBackend::quickAccess() const
{
    QStringList result;
    for (const QVariant &place : m_places) {
        const QString path = place.toMap().value(QStringLiteral("path")).toString();
        if (path != m_homePath)
            result.append(path);
    }
    return result;
}

// --- Formatowanie ---

QString FilesBackend::formatSize(double bytes) const
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(qRound64(bytes));
    static const char *units[] = {"KB", "MB", "GB", "TB"};
    double value = bytes / 1024;
    int unit = 0;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value < 10 ? QString::number(value, 'f', 1) : QString::number(qRound64(value)),
                                       QLatin1String(units[unit]));
}

QString FilesBackend::formatDate(double ms) const
{
    if (ms <= 0)
        return {};
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(qint64(ms)), QStringLiteral("dd.MM.yyyy HH:mm"));
}

QString FilesBackend::formatRelative(double ms) const
{
    if (ms <= 0)
        return {};
    const qint64 minutes = (QDateTime::currentMSecsSinceEpoch() - qint64(ms)) / 60000;
    if (minutes < 60)
        return minutes <= 1 ? tr("Just now") : tr("%1 minutes ago").arg(minutes);
    const qint64 hours = (minutes + 30) / 60;
    if (hours < 24)
        return hours == 1 ? tr("An hour ago") : tr("%1 hours ago").arg(hours);
    const qint64 days = (hours + 12) / 24;
    return days == 1 ? tr("Yesterday") : tr("%1 days ago").arg(days);
}

// --- Ikony i aplikacje ---

QString FilesBackend::themeIcon(const QString &iconName, const QString &mimeType, bool isDir) const
{
    const QString key = iconName + QLatin1Char('|') + mimeType;
    auto cached = m_iconCache.constFind(key);
    if (cached != m_iconCache.cend())
        return cached.value();

    QStringList candidates{iconName};
    if (isDir) {
        candidates.append(QStringLiteral("folder"));
    } else {
        // Baza typów MIME daje nazwę ikony i ikonę ogólną (bez dostępu do plików)
        static const QMimeDatabase database;
        const QMimeType type = database.mimeTypeForName(mimeType);
        if (type.isValid()) {
            candidates.append(type.iconName());
            candidates.append(type.genericIconName());
        }
        const QString group = mimeType.section(QLatin1Char('/'), 0, 0);
        if (group == QLatin1String("image") || group == QLatin1String("audio") || group == QLatin1String("video")
            || group == QLatin1String("text"))
            candidates.append(group + QLatin1String("-x-generic"));
        candidates.append(QStringLiteral("text-x-generic"));
        candidates.append(QStringLiteral("unknown"));
    }

    QString result = candidates.constLast();
    for (const QString &candidate : std::as_const(candidates)) {
        if (!candidate.isEmpty() && QIcon::hasThemeIcon(candidate)) {
            result = candidate;
            break;
        }
    }
    m_iconCache.insert(key, result);
    return result;
}

QString FilesBackend::folderIcon(const QString &path) const
{
    if (path == StartPage)
        return themeIcon(QStringLiteral("user-home"), {}, true);
    if (path == ComputerPage)
        return themeIcon(QStringLiteral("computer"), {}, true);
    if (path == QLatin1String("/"))
        return themeIcon(QStringLiteral("drive-harddisk"), {}, true);
    if (path == m_homePath)
        return themeIcon(QStringLiteral("user-home"), {}, true);
    return themeIcon(m_placeIcons.value(path, QStringLiteral("folder")), {}, true);
}

void FilesBackend::openFile(const QString &path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void FilesBackend::openTerminal(const QString &path) const
{
    QProcess::startDetached(QStringLiteral("konsole"), {QStringLiteral("--workdir"), path});
}
