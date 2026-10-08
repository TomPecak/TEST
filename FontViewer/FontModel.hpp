#include <QAbstractListModel>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

class FontModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit FontModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
        // Stała ścieżka do katalogu z czcionkami
        QString fontDirPath = "/home/rafal/Dokumenty/GITHUB_MOJE/KIKOFontViewer/build/Qt_6_8_1_dla_gcc_64-Debug/fonts/";

        // Iterujemy po plikach .ttf w katalogu
        QDir dir(fontDirPath);
        QFileInfoList fontFiles = dir.entryInfoList(QStringList() << "*.ttf", QDir::Files);

        // Przechowujemy ścieżki do czcionek w liście
        for (const QFileInfo &fileInfo : fontFiles) {
            m_fontFiles.append("file:" + fileInfo.absoluteFilePath());
        }

        qDebug() << "FONTS LIST: " << m_fontFiles;
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return m_fontFiles.count();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_fontFiles.count())
            return QVariant();

        if (role == Qt::DisplayRole) {
            qDebug() << "Returning font file:" << m_fontFiles.at(index.row());
            return m_fontFiles.at(index.row());
        }

        return QVariant();
    }

private:
    QStringList m_fontFiles;  // Lista ścieżek do plików czcionek
};
