#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

class FileManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY currentFolderChanged)
    Q_PROPERTY(QStringList videoFiles READ videoFiles NOTIFY videoFilesChanged)
    Q_PROPERTY(QStringList imageFiles READ imageFiles NOTIFY imageFilesChanged)
    Q_PROPERTY(QStringList mediaFiles READ mediaFiles NOTIFY mediaFilesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY hasPreviousChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY hasNextChanged)
    Q_PROPERTY(QString currentFileType READ currentFileType NOTIFY currentFileTypeChanged)

public:
    explicit FileManager(QObject *parent = nullptr);

    // 扫描文件夹中的媒体文件
    Q_INVOKABLE void scanFolder(const QString &folderPath);
    Q_INVOKABLE void scanFolderForFile(const QString &filePath);
    Q_INVOKABLE void scanFolderForMedia(const QString &filePath);
    Q_INVOKABLE QString getNextFile();
    Q_INVOKABLE QString getPreviousFile();
    Q_INVOKABLE QString getFileByIndex(int index);

    // 属性读取器
    QString currentFile() const { return m_currentFile; }
    QString currentFolder() const { return m_currentFolder; }
    QStringList videoFiles() const { return m_videoFiles; }
    QStringList imageFiles() const { return m_imageFiles; }
    QStringList mediaFiles() const { return m_mediaFiles; }
    int currentIndex() const { return m_currentIndex; }
    bool hasPrevious() const { return m_currentIndex > 0; }
    bool hasNext() const { return m_currentIndex < m_mediaFiles.size() - 1; }
    QString currentFileType() const { return m_currentFileType; }

signals:
    void currentFileChanged();
    void currentFolderChanged();
    void videoFilesChanged();
    void imageFilesChanged();
    void mediaFilesChanged();
    void currentIndexChanged();
    void hasPreviousChanged();
    void hasNextChanged();
    void currentFileTypeChanged();

private:
    QStringList getVideoFilesInFolder(const QString &folderPath);
    QStringList getImageFilesInFolder(const QString &folderPath);
    void setCurrentFile(const QString &filePath);
    void updateMediaFiles();

    QString m_currentFile;
    QString m_currentFolder;
    QString m_currentFileType; // "video", "image", "none"
    QStringList m_videoFiles;
    QStringList m_imageFiles;
    QStringList m_mediaFiles; // 合并的视频和图片文件列表
    int m_currentIndex = -1;
};

#endif // FILEMANAGER_H
