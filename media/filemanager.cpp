#include "filemanager.h"

namespace {
const QStringList &videoExtensions() {
    static const QStringList exts = {
        "mp4", "avi", "mkv", "mov", "wmv",
        "flv", "webm", "m4v", "3gp", "ts"
    };
    return exts;
}

QStringList videoExtensionPatterns() {
    QStringList patterns;
    for (const auto &ext : videoExtensions()) {
        patterns << "*." + ext;
    }
    return patterns;
}
} // namespace

FileManager::FileManager(QObject *parent)
    : QObject(parent)
{
}

void FileManager::scanFolder(const QString &folderPath)
{
    QDir dir(folderPath);
    if (!dir.exists()) {
        qWarning() << "Folder does not exist:" << folderPath;
        return;
    }

    m_currentFolder = folderPath;
    m_videoFiles = getVideoFilesInFolder(folderPath);
    m_currentIndex = -1;
    m_currentFile.clear();

    emit currentFolderChanged();
    emit videoFilesChanged();
    emit currentIndexChanged();
    emit hasPreviousChanged();
    emit hasNextChanged();
    emit currentFileChanged();

    qDebug() << "Scanned folder:" << folderPath << "Found" << m_videoFiles.size() << "video files";
}

void FileManager::scanFolderForFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "File does not exist:" << filePath;
        return;
    }
    const QString ext = fileInfo.suffix().toLower();
    if (!videoExtensions().contains(ext)) {
        qWarning() << "Not a supported video file:" << filePath;
        return;
    }

    QString folderPath = fileInfo.absolutePath();
    scanFolder(folderPath);
    setCurrentFile(filePath);
}

QString FileManager::getNextFile()
{
    if (hasNext()) {
        m_currentIndex++;
        QString nextFile = m_videoFiles.at(m_currentIndex);
        setCurrentFile(nextFile);
        return nextFile;
    }
    return QString();
}

QString FileManager::getPreviousFile()
{
    if (hasPrevious()) {
        m_currentIndex--;
        QString prevFile = m_videoFiles.at(m_currentIndex);
        setCurrentFile(prevFile);
        return prevFile;
    }
    return QString();
}

QString FileManager::getFileByIndex(int index)
{
    if (index >= 0 && index < m_videoFiles.size()) {
        m_currentIndex = index;
        QString file = m_videoFiles.at(index);
        setCurrentFile(file);
        return file;
    }
    return QString();
}

QStringList FileManager::getVideoFilesInFolder(const QString &folderPath)
{
    QStringList videoFiles;
    QDir dir(folderPath);

    // 获取所有视频文件
    QStringList files = dir.entryList(videoExtensionPatterns(), QDir::Files | QDir::Readable, QDir::Name);
    
    for (const QString &file : files) {
        videoFiles.append(dir.absoluteFilePath(file));
    }

    return videoFiles;
}

void FileManager::setCurrentFile(const QString &filePath)
{
    if (m_currentFile != filePath) {
        const QString ext = QFileInfo(filePath).suffix().toLower();
        if (!videoExtensions().contains(ext)) {
            qWarning() << "Skip non-video file:" << filePath;
            return;
        }

        m_currentFile = filePath;

        if (!m_videoFiles.contains(filePath)) {
            m_videoFiles.append(filePath);
            emit videoFilesChanged();
        }

        // 更新当前索引
        m_currentIndex = m_videoFiles.indexOf(filePath);
        
        emit currentFileChanged();
        emit currentIndexChanged();
        emit hasPreviousChanged();
        emit hasNextChanged();

        qDebug() << "Current file changed to:" << filePath << "Index:" << m_currentIndex;
    }
}
