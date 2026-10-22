#include "FolderModel.h"

#include "FilesBackend.h"

#include <QDebug>
#include <QDir>
#include <QSet>

#include <algorithm>

namespace {

bool isSpecialPage(const QString &path)
{
    return path.isEmpty() || !path.startsWith(QLatin1Char('/'));
}

// Porównanie "naturalne": bez wielkości liter, ciągi cyfr porównywane jako liczby (plik2 < plik10)
int naturalCompare(const QString &a, const QString &b)
{
    const int lengthA = a.size();
    const int lengthB = b.size();
    int i = 0;
    int j = 0;
    while (i < lengthA && j < lengthB) {
        const QChar ca = a.at(i);
        const QChar cb = b.at(j);
        if (ca.isDigit() && cb.isDigit()) {
            int endA = i;
            int endB = j;
            while (endA < lengthA && a.at(endA).isDigit())
                ++endA;
            while (endB < lengthB && b.at(endB).isDigit())
                ++endB;
            // Pomijamy zera wiodące, potem dłuższa liczba jest większa
            int startA = i;
            int startB = j;
            while (startA < endA - 1 && a.at(startA) == QLatin1Char('0'))
                ++startA;
            while (startB < endB - 1 && b.at(startB) == QLatin1Char('0'))
                ++startB;
            const int digitsA = endA - startA;
            const int digitsB = endB - startB;
            if (digitsA != digitsB)
                return digitsA < digitsB ? -1 : 1;
            for (int k = 0; k < digitsA; ++k) {
                const QChar da = a.at(startA + k);
                const QChar db = b.at(startB + k);
                if (da != db)
                    return da < db ? -1 : 1;
            }
            i = endA;
            j = endB;
            continue;
        }
        const QChar fa = ca.toCaseFolded();
        const QChar fb = cb.toCaseFolded();
        if (fa != fb)
            return fa < fb ? -1 : 1;
        ++i;
        ++j;
    }
    if (lengthA - i != lengthB - j)
        return (lengthA - i) < (lengthB - j) ? -1 : 1;
    return QString::compare(a, b); // te same litery, inna wielkość: stała kolejność
}

} // namespace

FolderModel::FolderModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_flushTimer.setSingleShot(true);
    connect(&m_flushTimer, &QTimer::timeout, this, &FolderModel::flushPending);
    m_staleTimer.setSingleShot(true);
    m_staleTimer.setInterval(150);
    connect(&m_staleTimer, &QTimer::timeout, this, [this] {
        if (m_replacing)
            clearContent(); // dane się spóźniają: pusty widok z "wczytywaniem" zamiast starego folderu
    });
    connect(FilesBackend::instance()->connection(), &ServiceConnection::connectedChanged, this, [this](bool connected) {
        if (connected)
            subscribe(); // po restarcie usługi folder subskrybujemy od nowa
        else
            m_subscription = 0;
    });
}

FolderModel::~FolderModel()
{
    if (auto *backend = FilesBackend::instance())
        backend->connection()->unsubscribe(m_subscription);
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const FileEntry &entry = m_entries.at(index.row());
    switch (role) {
    case NameRole:
        return entry.name;
    case PathRole:
        return QDir(entry.location.isEmpty() ? m_contentPath : entry.location).filePath(entry.name);
    case IsDirRole:
        return entry.isDir;
    case IsSymlinkRole:
        return entry.isSymlink;
    case IsHiddenRole:
        return entry.isHidden;
    case SizeRole:
        return double(entry.size);
    case ModifiedRole:
        return double(entry.modified);
    case MimeTypeRole:
        return entry.mimeType;
    case IconNameRole:
        return entry.iconName;
    case TypeNameRole:
        return entry.typeName;
    case LocationRole:
        return m_contentQuery.isEmpty() ? QString() : entry.location;
    }
    return {};
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {IsDirRole, "isDir"},
        {IsSymlinkRole, "isSymlink"},
        {IsHiddenRole, "isHidden"},
        {SizeRole, "size"},
        {ModifiedRole, "modified"},
        {MimeTypeRole, "mimeType"},
        {IconNameRole, "iconName"},
        {TypeNameRole, "typeName"},
        {LocationRole, "location"},
    };
}

// --- Właściwości ---

void FolderModel::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    emit pathChanged();
    subscribe();
}

void FolderModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
    subscribe();
}

void FolderModel::setSortRole(const QString &role)
{
    if (m_sortRole == role)
        return;
    m_sortRole = role;
    emit sortChanged();
    resort();
}

void FolderModel::setSortAscending(bool ascending)
{
    if (m_sortAscending == ascending)
        return;
    m_sortAscending = ascending;
    emit sortChanged();
    resort();
}

void FolderModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    emit showHiddenChanged();
    rebuild();
}

void FolderModel::reload()
{
    subscribe();
}

void FolderModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void FolderModel::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

// --- Subskrypcja ---

void FolderModel::subscribe()
{
    ServiceConnection *connection = FilesBackend::instance()->connection();
    connection->unsubscribe(m_subscription);
    m_subscription = 0;

    m_pending.clear();
    m_flushTimer.stop();
    m_snapshotDone = false;
    m_firstFlushDone = false;
    m_measuredFirstMs = 0;
    m_serviceFirstMs = -1;
    m_serviceAllMs = -1;
    setError({});

    // Strona "start" / "computer": wiersze nie są potrzebne, widok przełącza się od razu
    if (isSpecialPage(m_path)) {
        clearContent();
        setLoading(false);
        return;
    }
    if (!connection->isConnected()) {
        // Bez usługi zostawiamy zawartość tylko wtedy, gdy to nadal ten sam folder
        if (m_contentPath != m_path || m_contentQuery != m_query)
            clearContent();
        setLoading(false);
        return;
    }

    setLoading(true);
    m_timer.start();
    // Obecne wiersze zostają do nadejścia nowych (bez pustej klatki). Termin liczymy od pierwszej
    // zmiany, więc szybkie przeskakiwanie po folderach nie trzyma starej zawartości w nieskończoność.
    if (!m_replacing) {
        m_replacing = true;
        emit contentChanged();
    }
    if (!m_staleTimer.isActive())
        m_staleTimer.start();
    m_subscription = m_query.isEmpty() ? connection->watchDirectory(m_path, this)
                                       : connection->search(m_path, m_query, this);
}

// Podmiana całej zawartości w jednym kroku: w żadnej klatce widok nie jest pusty
void FolderModel::replaceContent(const QVector<FileEntry> &entries)
{
    QVector<FileEntry> visible;
    visible.reserve(entries.size());
    for (const FileEntry &entry : entries) {
        if (m_showHidden || !entry.isHidden)
            visible.append(entry);
    }
    std::sort(visible.begin(), visible.end(), [this](const FileEntry &a, const FileEntry &b) { return lessThan(a, b); });
    beginResetModel();
    m_all = entries;
    m_entries = std::move(visible);
    m_contentPath = m_path;
    m_contentQuery = m_query;
    m_replacing = false;
    endResetModel();
    m_staleTimer.stop();
    bumpRevision();
    emit countChanged();
    emit contentChanged();
}

void FolderModel::clearContent()
{
    m_staleTimer.stop();
    const bool hadRows = !m_all.isEmpty();
    if (hadRows) {
        beginResetModel();
        m_all.clear();
        m_entries.clear();
        endResetModel();
    }
    const bool changed = hadRows || m_replacing || m_contentPath != m_path || m_contentQuery != m_query;
    m_contentPath = m_path;
    m_contentQuery = m_query;
    m_replacing = false;
    if (changed) {
        bumpRevision();
        emit countChanged();
        emit contentChanged();
    }
}

void FolderModel::directoryEvent(const DirectoryEvent &event)
{
    switch (event.type) {
    case DirectoryEvent::Batch:
        for (const FileEntry &entry : event.entries)
            m_pending.append(prepared(entry));
        if (m_serviceFirstMs < 0)
            m_serviceFirstMs = event.serviceMs;
        if (!m_firstFlushDone) {
            m_measuredFirstMs = m_timer.nsecsElapsed() / 1e6;
            flushPending(); // pierwsza partia od razu
        } else if (!m_flushTimer.isActive()) {
            m_flushTimer.start(100);
        }
        break;
    case DirectoryEvent::Done:
        flushPending();
        if (m_replacing)
            replaceContent({}); // folder jest pusty
        m_snapshotDone = true;
        m_serviceAllMs = event.serviceMs;
        // Czasy podmieniamy razem, żeby pasek stanu nie mignął pustą wartością
        m_loadMs = m_timer.nsecsElapsed() / 1e6;
        m_firstBatchMs = m_measuredFirstMs > 0 ? m_measuredFirstMs : m_loadMs;
        emit timingChanged();
        setLoading(false);
        if (m_timer.isValid())
            qInfo().noquote() << QStringLiteral("[files] %1%2: %3 entries | GUI got first batch %4 ms, all %5 ms"
                                            " | RPC thread got first %6 ms, all %7 ms")
                                 .arg(m_path, m_query.isEmpty() ? QString() : QStringLiteral(" ?") + m_query)
                                 .arg(m_all.size())
                                 .arg(m_firstBatchMs, 0, 'f', 1)
                                 .arg(m_loadMs, 0, 'f', 1)
                                 .arg(m_serviceFirstMs, 0, 'f', 1)
                                 .arg(m_serviceAllMs, 0, 'f', 1);
        break;
    case DirectoryEvent::Added:
    case DirectoryEvent::Changed: {
        // Wpis, który już jest (zmiana rozmiaru, daty; powtórzone "added"), aktualizujemy w miejscu;
        // nowe nazwy dopisujemy
        QVector<FileEntry> fresh;
        for (const FileEntry &entry : event.entries) {
            const FileEntry ready = prepared(entry);
            if (!updateEntry(ready))
                fresh.append(ready);
        }
        if (fresh.size() > 32) {
            appendEntries(fresh);
        } else if (!fresh.isEmpty()) {
            for (const FileEntry &entry : std::as_const(fresh)) {
                m_all.append(entry);
                if (m_showHidden || !entry.isHidden)
                    insertSorted(entry);
            }
            bumpRevision();
            emit countChanged();
        } else {
            bumpRevision();
        }
        if (event.type == DirectoryEvent::Added) {
            for (const FileEntry &entry : event.entries)
                emit entryAdded(entry.name);
        }
        break;
    }
    case DirectoryEvent::Removed:
        removeNames(event.names);
        bumpRevision();
        emit countChanged();
        break;
    case DirectoryEvent::Failed:
        if (m_replacing)
            clearContent();
        setError(event.message);
        setLoading(false);
        break;
    }
}

FileEntry FolderModel::prepared(FileEntry entry) const
{
    // Ikonę wybiera frontend: konkretna z motywu albo ogólna dla typu, gdy motyw jej nie ma
    entry.iconName = FilesBackend::instance()->themeIcon(entry.iconName, entry.mimeType, entry.isDir);
    return entry;
}

void FolderModel::flushPending()
{
    m_flushTimer.stop();
    if (m_pending.isEmpty())
        return;
    m_firstFlushDone = true;
    if (m_replacing)
        replaceContent(m_pending);
    else
        appendEntries(m_pending);
    m_pending.clear();
}

// --- Sortowanie i zmiany ---

bool FolderModel::lessThan(const FileEntry &a, const FileEntry &b) const
{
    if (a.isDir != b.isDir)
        return a.isDir; // foldery zawsze na górze
    int result = 0;
    if (m_sortRole == QLatin1String("modified"))
        result = a.modified < b.modified ? -1 : a.modified > b.modified ? 1 : 0;
    else if (m_sortRole == QLatin1String("size"))
        result = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
    else if (m_sortRole == QLatin1String("type"))
        result = QString::compare(a.typeName, b.typeName, Qt::CaseInsensitive);
    if (result == 0)
        result = naturalCompare(a.name, b.name);
    return m_sortAscending ? result < 0 : result > 0;
}

void FolderModel::rebuild()
{
    QVector<FileEntry> visible;
    visible.reserve(m_all.size());
    for (const FileEntry &entry : std::as_const(m_all)) {
        if (m_showHidden || !entry.isHidden)
            visible.append(entry);
    }
    std::sort(visible.begin(), visible.end(), [this](const FileEntry &a, const FileEntry &b) { return lessThan(a, b); });
    beginResetModel();
    m_entries = std::move(visible);
    endResetModel();
    bumpRevision();
    emit countChanged();
}

// Nowa kolejność bez resetu: widok zachowuje pozycję przewinięcia
void FolderModel::resort()
{
    emit layoutAboutToBeChanged();
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [this](const FileEntry &a, const FileEntry &b) { return lessThan(a, b); });
    emit layoutChanged();
    bumpRevision();
}

// Dużo wpisów naraz (partia migawki, kopiowanie wielu plików): dopisanie na końcu i jedno
// sortowanie zamiast wstawiania każdego wiersza osobno
void FolderModel::appendEntries(const QVector<FileEntry> &entries)
{
    QVector<FileEntry> visible;
    for (const FileEntry &entry : entries) {
        m_all.append(entry);
        if (m_showHidden || !entry.isHidden)
            visible.append(entry);
    }
    if (!visible.isEmpty()) {
        const int first = m_entries.size();
        beginInsertRows({}, first, first + visible.size() - 1);
        m_entries += visible;
        endInsertRows();
        resort();
    }
    bumpRevision();
    emit countChanged();
}

void FolderModel::removeNames(const QStringList &names)
{
    if (names.size() <= 32) {
        for (const QString &name : names)
            removeName(name);
        return;
    }
    const QSet<QString> gone(names.cbegin(), names.cend());
    m_all.removeIf([&](const FileEntry &e) { return gone.contains(e.name); });
    // Usuwamy ciągłe zakresy wierszy od końca
    int row = m_entries.size() - 1;
    while (row >= 0) {
        if (!gone.contains(m_entries.at(row).name)) {
            --row;
            continue;
        }
        int first = row;
        while (first > 0 && gone.contains(m_entries.at(first - 1).name))
            --first;
        beginRemoveRows({}, first, row);
        m_entries.remove(first, row - first + 1);
        endRemoveRows();
        row = first - 1;
    }
}

void FolderModel::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
}

// Aktualizacja istniejącego wpisu bez usuwania wiersza: dataChanged, a gdy zmiana przesuwa wpis
// w sortowaniu (np. sortowanie po rozmiarze lub dacie) - jedno przesunięcie wiersza.
// Zwraca false, gdy takiej nazwy jeszcze nie ma.
bool FolderModel::updateEntry(const FileEntry &entry)
{
    auto stored = std::find_if(m_all.begin(), m_all.end(), [&](const FileEntry &e) { return e.name == entry.name; });
    if (stored == m_all.end())
        return false;
    *stored = entry;

    const bool visible = m_showHidden || !entry.isHidden;
    int row = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).name == entry.name) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        if (visible) {
            insertSorted(entry);
            emit countChanged();
        }
        return true;
    }
    if (!visible) {
        beginRemoveRows({}, row, row);
        m_entries.removeAt(row);
        endRemoveRows();
        emit countChanged();
        return true;
    }

    // Czy wpis nadal stoi między sąsiadami?
    const int last = m_entries.size() - 1;
    const bool inPlace = (row == 0 || !lessThan(entry, m_entries.at(row - 1)))
                         && (row == last || !lessThan(m_entries.at(row + 1), entry));
    if (inPlace) {
        m_entries[row] = entry;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed);
        return true;
    }

    // Docelowa pozycja liczona w liście bez tego wpisu
    m_entries.removeAt(row);
    const int target = insertPosition(entry);
    m_entries.insert(row, entry);
    if (target == row) {
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed);
        return true;
    }
    // Qt oczekuje indeksu "przed którym" wstawić w starym układzie
    beginMoveRows({}, row, row, {}, target > row ? target + 1 : target);
    m_entries.move(row, target);
    endMoveRows();
    const QModelIndex changed = index(target);
    emit dataChanged(changed, changed);
    return true;
}

int FolderModel::insertPosition(const FileEntry &entry) const
{
    auto it = std::lower_bound(m_entries.cbegin(), m_entries.cend(), entry,
                               [this](const FileEntry &a, const FileEntry &b) { return lessThan(a, b); });
    return int(it - m_entries.cbegin());
}

void FolderModel::insertSorted(const FileEntry &entry)
{
    const int row = insertPosition(entry);
    beginInsertRows({}, row, row);
    m_entries.insert(row, entry);
    endInsertRows();
}

void FolderModel::removeName(const QString &name)
{
    m_all.removeIf([&](const FileEntry &e) { return e.name == name; });
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).name == name) {
            beginRemoveRows({}, row, row);
            m_entries.removeAt(row);
            endRemoveRows();
            return;
        }
    }
}

// --- Pomocnicze dla QML ---

QVariantMap FolderModel::toMap(const FileEntry &entry) const
{
    const QString location = entry.location.isEmpty() ? m_contentPath : entry.location;
    return {
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("path"), QDir(location).filePath(entry.name)},
        {QStringLiteral("location"), location},
        {QStringLiteral("isDir"), entry.isDir},
        {QStringLiteral("isSymlink"), entry.isSymlink},
        {QStringLiteral("isHidden"), entry.isHidden},
        {QStringLiteral("size"), double(entry.size)},
        {QStringLiteral("modified"), double(entry.modified)},
        {QStringLiteral("mimeType"), entry.mimeType},
        {QStringLiteral("iconName"), entry.iconName},
        {QStringLiteral("typeName"), entry.typeName},
    };
}

QVariantMap FolderModel::get(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return toMap(m_entries.at(row));
}

int FolderModel::indexOf(const QString &name) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).name == name)
            return row;
    }
    return -1;
}

QStringList FolderModel::names(int from, int to) const
{
    if (to < 0 || to >= m_entries.size())
        to = m_entries.size() - 1;
    QStringList result;
    for (int row = qMax(0, from); row <= to; ++row)
        result.append(m_entries.at(row).name);
    return result;
}

QVariantList FolderModel::entries(const QStringList &names) const
{
    QVariantList result;
    if (names.isEmpty())
        return result;
    const QSet<QString> wanted(names.cbegin(), names.cend());
    for (const FileEntry &entry : m_entries) {
        if (wanted.contains(entry.name))
            result.append(toMap(entry));
    }
    return result;
}

double FolderModel::totalSize(const QStringList &names) const
{
    if (names.isEmpty())
        return 0;
    const QSet<QString> wanted(names.cbegin(), names.cend());
    double sum = 0;
    for (const FileEntry &entry : m_entries) {
        if (!entry.isDir && wanted.contains(entry.name))
            sum += double(entry.size);
    }
    return sum;
}

QStringList FolderModel::existing(const QStringList &names) const
{
    if (names.isEmpty())
        return {};
    QSet<QString> present;
    present.reserve(m_entries.size());
    for (const FileEntry &entry : m_entries)
        present.insert(entry.name);
    QStringList result;
    for (const QString &name : names) {
        if (present.contains(name))
            result.append(name);
    }
    return result;
}
