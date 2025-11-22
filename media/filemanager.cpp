#include "filemanager.h"

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
    m_imageFiles = getImageFilesInFolder(folderPath);
    m_audioFiles = getAudioFilesInFolder(folderPath);
    updateMediaFiles();
    m_currentIndex = -1;
    m_currentFile.clear();
    m_currentFileType = "none";

    emit currentFolderChanged();
    emit videoFilesChanged();
    emit imageFilesChanged();
    emit audioFilesChanged();
    emit mediaFilesChanged();
    emit currentIndexChanged();
    emit hasPreviousChanged();
    emit hasNextChanged();
    emit currentFileChanged();
    emit currentFileTypeChanged();

    qDebug() << "Scanned folder:" << folderPath << "Found" << m_videoFiles.size() << "video files," << m_imageFiles.size() << "image files," << m_audioFiles.size() << "audio files";
}

void FileManager::scanFolderForFile(const QString &filePath)
{
    scanFolderForMedia(filePath);
}

void FileManager::scanFolderForMedia(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "File does not exist:" << filePath;
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
        QString nextFile = m_mediaFiles.at(m_currentIndex);
        setCurrentFile(nextFile);
        return nextFile;
    }
    return QString();
}

QString FileManager::getPreviousFile()
{
    if (hasPrevious()) {
        m_currentIndex--;
        QString prevFile = m_mediaFiles.at(m_currentIndex);
        setCurrentFile(prevFile);
        return prevFile;
    }
    return QString();
}

QString FileManager::getFileByIndex(int index)
{
    if (index >= 0 && index < m_mediaFiles.size()) {
        m_currentIndex = index;
        QString file = m_mediaFiles.at(index);
        setCurrentFile(file);
        return file;
    }
    return QString();
}

QStringList FileManager::getVideoFilesInFolder(const QString &folderPath)
{
    QStringList videoFiles;
    QDir dir(folderPath);

    // 支持的视频文件扩展名
    QStringList videoExtensions = {
        "*.mp4", "*.avi", "*.mkv", "*.mov", "*.wmv", 
        "*.flv", "*.webm", "*.m4v", "*.3gp", "*.ts"
    };

    // 获取所有视频文件
    QStringList files = dir.entryList(videoExtensions, QDir::Files | QDir::Readable, QDir::Name);
    
    for (const QString &file : files) {
        videoFiles.append(dir.absoluteFilePath(file));
    }

    return videoFiles;
}

QStringList FileManager::getImageFilesInFolder(const QString &folderPath)
{
    QStringList imageFiles;
    QDir dir(folderPath);

    // 支持的图片文件扩展名
    QStringList imageExtensions = {
        "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif",
        "*.tiff", "*.tif", "*.webp", "*.ico", "*.svg"
    };

    // 获取所有图片文件
    QStringList files = dir.entryList(imageExtensions, QDir::Files | QDir::Readable, QDir::Name);
    
    for (const QString &file : files) {
        imageFiles.append(dir.absoluteFilePath(file));
    }

    return imageFiles;
}

QStringList FileManager::getAudioFilesInFolder(const QString &folderPath)
{
    QStringList audioFiles;
    QDir dir(folderPath);

    // 支持的音频文件扩展名
    QStringList audioExtensions = {
        "*.mp3", "*.wav", "*.flac", "*.aac", "*.ogg",
        "*.m4a", "*.wma", "*.opus", "*.aiff", "*.ape"
    };

    // 获取所有音频文件
    QStringList files = dir.entryList(audioExtensions, QDir::Files | QDir::Readable, QDir::Name);
    
    for (const QString &file : files) {
        audioFiles.append(dir.absoluteFilePath(file));
    }

    return audioFiles;
}

void FileManager::updateMediaFiles()
{
    m_mediaFiles.clear();
    
    // 合并视频、图片和音频文件，按文件名排序
    m_mediaFiles.append(m_videoFiles);
    m_mediaFiles.append(m_imageFiles);
    m_mediaFiles.append(m_audioFiles);
    
}

void FileManager::setCurrentFile(const QString &filePath)
{
    if (m_currentFile != filePath) {
        m_currentFile = filePath;
        
        // 更新当前索引
        m_currentIndex = m_mediaFiles.indexOf(filePath);
        
        // 判断文件类型
        QString extension = QFileInfo(filePath).suffix().toLower();
        if (m_videoFiles.contains(filePath)) {
            m_currentFileType = "video";
        } else if (m_imageFiles.contains(filePath)) {
            m_currentFileType = "image";
        } else if (m_audioFiles.contains(filePath)) {
            m_currentFileType = "audio";
        } else {
            m_currentFileType = "unknown";
        }
        
        emit currentFileChanged();
        emit currentIndexChanged();
        emit hasPreviousChanged();
        emit hasNextChanged();
        emit currentFileTypeChanged();

        qDebug() << "Current file changed to:" << filePath << "Type:" << m_currentFileType << "Index:" << m_currentIndex;
    }
}
