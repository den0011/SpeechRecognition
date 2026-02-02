#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>

#include "speechrecognizer.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void audioDevicesRefreshed(const QStringList &devices);

private slots:
    void onRecordButtonClicked();
    void onRecognitionComplete(const QString &text);
    void onError(const QString &error);
    void onRecordingStarted();
    void onRecordingStopped();
    void onSettingsClicked();
    void onClearLogsClicked();
    void updateRecordingTimer();
    void onRefreshAudioDevices();
    void onOpenRecordingsClicked();
    void onAudioConfigClicked();
    void onLoadFileClicked();

private:
    void loadSettings();
    void saveSettings();
    void checkModelExists();
    void showModelNotFoundWarning();
    bool checkComponentsBeforeRecording();
    void addLog(const QString &message, const QString &level = "INFO");
    void applyAudioSettings();

private:
    Ui::MainWindow *ui;
    SpeechRecognizer *m_recognizer;
    bool m_isRecording;
    QTimer *m_recordingTimer;
    QElapsedTimer m_elapsedTimer;
};

#endif // MAINWINDOW_H
