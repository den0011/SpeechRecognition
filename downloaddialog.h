#ifndef DOWNLOADDIALOG_H
#define DOWNLOADDIALOG_H

#include <QDialog>
#include <QProcess>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;

// Скачивание whisper.cpp (whisper-cli + whisper-server) и моделей прямо из программы.
// Загрузка идёт через системные curl и tar (есть в Windows 10/11), поэтому
// не нужны библиотеки OpenSSL для Qt.
class DownloadDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DownloadDialog(const QString &currentWhisperPath, const QString &currentModelPath,
                            QWidget *parent = nullptr);
    ~DownloadDialog();

    QString whisperPath() const;
    QString modelPath() const;

    static QString installDirectory();

private slots:
    void onStartClicked();
    void onCancelClicked();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProgressTick();

private:
    enum Step { Idle, FetchRelease, DownloadBinaries, ExtractBinaries, DownloadModel };

    struct ModelInfo {
        QString file;
        QString title;
        qint64 size;
    };

    void refreshState();
    void nextStep();
    void fetchRelease();
    void downloadFile(const QString &url, const QString &target, qint64 expectedSize);
    void extractBinaries();
    void finishAll();
    void fail(const QString &message);
    void appendLog(const QString &message);
    void setBusy(bool busy);
    QString findWhisperCli() const;
    QString selectedModelFile() const;
    qint64 selectedModelSize() const;
    static QString binaryArchiveName();
    static QString formatSize(qint64 bytes);

private:
    QCheckBox *m_binCheck;
    QLabel *m_binStatus;
    QComboBox *m_modelCombo;
    QLabel *m_modelStatus;
    QProgressBar *m_progress;
    QLabel *m_stepLabel;
    QTextEdit *m_log;
    QPushButton *m_startButton;
    QPushButton *m_closeButton;

    QList<ModelInfo> m_models;
    QProcess *m_process;
    QTimer m_progressTimer;
    Step m_step;
    QString m_downloadTarget;
    qint64 m_expectedSize;
    qint64 m_lastSize;
    qint64 m_lastTick;
    QString m_assetUrl;
    qint64 m_assetSize;
    QString m_releaseTag;
    QString m_whisperPath;
    QString m_modelPath;
    bool m_cancelled;
};

#endif // DOWNLOADDIALOG_H
