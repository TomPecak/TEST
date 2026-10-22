#pragma once

// Model zawartości folderu (albo wyników wyszukiwania) dla widoków QML.
//
// Dane przychodzą z usługi partiami; pierwsza partia jest pokazywana od razu, kolejne są łączone
// co najwyżej co 100 ms, żeby duży folder nie przebudowywał widoku przy każdej partii. Po migawce
// zmiany na żywo (inotify) są nanoszone pojedynczymi wierszami, bez resetu widoku.
// Sortowanie: foldery najpierw, potem według sortRole; nazwy "naturalnie" (plik2 < plik10).
//
// Bez pustej klatki: po zmianie folderu stara zawartość zostaje, dopóki nie przyjdzie pierwsza
// partia nowej (albo informacja, że folder jest pusty), a podmiana idzie jednym resetem. Gdy dane
// nie przyjdą w ciągu 150 ms (wolny dysk, sieć), widok jest czyszczony i pokazuje wczytywanie.
// contentPath / contentQuery mówią, czego dotyczą wiersze widoczne w tej chwili.

#include "ServiceConnection.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class FolderModel : public QAbstractListModel, public DirectoryReceiver
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString sortRole READ sortRole WRITE setSortRole NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // Folder (albo strona "start" / "computer") i wyszukiwanie, do których należą obecne wiersze
    Q_PROPERTY(QString contentPath READ contentPath NOTIFY contentChanged)
    Q_PROPERTY(QString contentQuery READ contentQuery NOTIFY contentChanged)
    // Wiersze są jeszcze z poprzedniego folderu (czekamy na nowy): kliknięcia należy pominąć
    Q_PROPERTY(bool stale READ stale NOTIFY contentChanged)
    // Rośnie po każdej zmianie zawartości (do wiązań zależnych od get() / entries())
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    // Czas od zlecenia do pierwszej partii i do pełnej migawki (ms), do podglądu płynności
    Q_PROPERTY(double firstBatchMs READ firstBatchMs NOTIFY timingChanged)
    Q_PROPERTY(double loadMs READ loadMs NOTIFY timingChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        IsSymlinkRole,
        IsHiddenRole,
        SizeRole,
        ModifiedRole,
        MimeTypeRole,
        IconNameRole,
        TypeNameRole,
        LocationRole,
    };

    explicit FolderModel(QObject *parent = nullptr);
    ~FolderModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    void setPath(const QString &path);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString sortRole() const { return m_sortRole; }
    void setSortRole(const QString &role);
    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool ascending);
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);
    int count() const { return m_entries.size(); }
    int revision() const { return m_revision; }
    int hiddenCount() const { return m_all.size() - m_entries.size(); }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QString contentPath() const { return m_contentPath; }
    QString contentQuery() const { return m_contentQuery; }
    bool stale() const { return m_replacing; }
    double firstBatchMs() const { return m_firstBatchMs; }
    double loadMs() const { return m_loadMs; }

    // Pomocnicze dla QML (zaznaczanie po nazwach, panel szczegółów)
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString &name) const;
    Q_INVOKABLE QStringList names(int from = 0, int to = -1) const;
    Q_INVOKABLE QVariantList entries(const QStringList &names) const;
    Q_INVOKABLE double totalSize(const QStringList &names) const;
    // Te z podanych nazw, które nadal są w modelu (po usunięciu albo zmianie nazwy)
    Q_INVOKABLE QStringList existing(const QStringList &names) const;
    Q_INVOKABLE void reload();

    void directoryEvent(const DirectoryEvent &event) override;

signals:
    void pathChanged();
    void queryChanged();
    void sortChanged();
    void showHiddenChanged();
    void countChanged();
    void revisionChanged();
    void loadingChanged();
    void errorChanged();
    void timingChanged();
    void contentChanged();
    // Wpis pojawił się w modelu (np. po utworzeniu nowego folderu): QML może go zaznaczyć
    void entryAdded(const QString &name);

private:
    void subscribe();
    void flushPending();
    void replaceContent(const QVector<FileEntry> &entries);
    void clearContent();
    void rebuild();
    void resort();
    void appendEntries(const QVector<FileEntry> &entries);
    void removeNames(const QStringList &names);
    bool lessThan(const FileEntry &a, const FileEntry &b) const;
    int insertPosition(const FileEntry &entry) const;
    void insertSorted(const FileEntry &entry);
    void removeName(const QString &name);
    bool updateEntry(const FileEntry &entry);
    void bumpRevision();
    void setLoading(bool loading);
    void setError(const QString &error);
    QVariantMap toMap(const FileEntry &entry) const;
    FileEntry prepared(FileEntry entry) const;

    QString m_path;
    QString m_query;
    QString m_sortRole = QStringLiteral("name");
    bool m_sortAscending = true;
    bool m_showHidden = false;
    bool m_loading = false;
    QString m_error;

    QVector<FileEntry> m_all;      // wszystkie wpisy (także ukryte)
    QVector<FileEntry> m_entries;  // widoczne, posortowane
    QVector<FileEntry> m_pending;  // partie migawki czekające na połączenie
    bool m_snapshotDone = false;
    bool m_firstFlushDone = false;
    QTimer m_flushTimer;
    QString m_contentPath;
    QString m_contentQuery;
    bool m_replacing = false;   // widoczne wiersze czekają na podmianę
    QTimer m_staleTimer;        // po jego upływie przestajemy pokazywać starą zawartość
    double m_measuredFirstMs = 0;

    quint64 m_subscription = 0;
    int m_revision = 0;
    QElapsedTimer m_timer;
    double m_firstBatchMs = 0;
    double m_loadMs = 0;
    double m_serviceFirstMs = -1;
    double m_serviceAllMs = -1;
};
