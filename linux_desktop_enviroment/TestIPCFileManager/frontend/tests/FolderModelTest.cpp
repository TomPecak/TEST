// Testy FolderModel: zdarzenia z usługi podawane bezpośrednio do modelu (bez usługi i bez IPC).

#include "FilesBackend.h"
#include "FolderModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

namespace {

FileEntry file(const QString &name, quint64 size, qint64 modified = 1000)
{
    FileEntry entry;
    entry.name = name;
    entry.size = size;
    entry.modified = modified;
    entry.isHidden = name.startsWith(QLatin1Char('.'));
    entry.mimeType = QStringLiteral("text/plain");
    entry.typeName = QStringLiteral("Text file");
    return entry;
}

DirectoryEvent makeEvent(DirectoryEvent::Type type, const QVector<FileEntry> &entries = {})
{
    DirectoryEvent e;
    e.type = type;
    e.entries = entries;
    return e;
}

} // namespace

class FolderModelTest : public QObject
{
    Q_OBJECT

private:
    // Model z migawką a, b, c (bez połączenia z usługą: dane podajemy sami)
    void load(FolderModel &model, const QVector<FileEntry> &entries)
    {
        model.directoryEvent(makeEvent(DirectoryEvent::Batch, entries));
        model.directoryEvent(makeEvent(DirectoryEvent::Done));
    }

private slots:
    void growingFileUpdatesRowInPlace()
    {
        FolderModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        load(model, {file("a.txt", 1), file("b.txt", 1), file("c.txt", 1)});

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy count(&model, &FolderModel::countChanged);

        // Kopiowanie: b.txt rośnie 25 razy
        for (int i = 1; i <= 25; ++i)
            model.directoryEvent(makeEvent(DirectoryEvent::Changed, {file("b.txt", i * 1000000)}));

        QCOMPARE(changed.count(), 25);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(moved.count(), 0);
        QCOMPARE(count.count(), 0);
        QCOMPARE(model.names(), QStringList({"a.txt", "b.txt", "c.txt"}));
        QCOMPARE(model.get(1).value("size").toDouble(), 25000000.0);
        // dataChanged dotyczy dokładnie wiersza b.txt
        QCOMPARE(changed.last().at(0).toModelIndex().row(), 1);
    }

    void changeThatReordersMovesOneRow()
    {
        FolderModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setSortRole("size");
        load(model, {file("a", 10), file("b", 20), file("c", 30), file("d", 40)});
        QCOMPARE(model.names(), QStringList({"a", "b", "c", "d"}));

        QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

        // a rośnie: przesuwa się w dół między c i d
        model.directoryEvent(makeEvent(DirectoryEvent::Changed, {file("a", 35)}));
        QCOMPARE(model.names(), QStringList({"b", "c", "a", "d"}));
        // d maleje: przesuwa się na górę
        model.directoryEvent(makeEvent(DirectoryEvent::Changed, {file("d", 1)}));
        QCOMPARE(model.names(), QStringList({"d", "b", "c", "a"}));
        // a rośnie, ale zostaje na końcu: bez przesunięcia
        model.directoryEvent(makeEvent(DirectoryEvent::Changed, {file("a", 99)}));
        QCOMPARE(model.names(), QStringList({"d", "b", "c", "a"}));

        QCOMPARE(moved.count(), 2);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(inserted.count(), 0);
    }

    void unknownOrRepeatedNames()
    {
        FolderModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        load(model, {file("a", 1), file("c", 1)});

        // "changed" dla nazwy, której nie ma: wstawienie w odpowiednim miejscu
        model.directoryEvent(makeEvent(DirectoryEvent::Changed, {file("b", 1)}));
        QCOMPARE(model.names(), QStringList({"a", "b", "c"}));

        // powtórzone "added": aktualizacja w miejscu, bez duplikatu
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy added(&model, &FolderModel::entryAdded);
        model.directoryEvent(makeEvent(DirectoryEvent::Added, {file("b", 5)}));
        QCOMPARE(model.count(), 3);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(added.count(), 1);

        // ukryty plik zmienia się, gdy ukryte nie są pokazywane: nie pojawia się na liście
        model.directoryEvent(makeEvent(DirectoryEvent::Added, {file(".hidden", 1)}));
        model.directoryEvent(makeEvent(DirectoryEvent::Changed, {file(".hidden", 2)}));
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.hiddenCount(), 1);
        model.setShowHidden(true);
        QCOMPARE(model.count(), 4);
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // Usługa nie jest potrzebna: połączenie po prostu się nie uda
    FilesBackend backend(QStringLiteral("/nonexistent/test-files.sock"));
    FolderModelTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "FolderModelTest.moc"
