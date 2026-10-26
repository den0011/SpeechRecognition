#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>

#include "speechrecognizer.h"
#include "realtimerecognizer.h"
#include "dictation.h"

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
    void onRealtimeFinalText(const QString &text);
    void onRealtimeFinished();
    void onDownloadClicked();
    void onHotkeyActivated();
    void onNoSignal();
    void onCopyTextClicked();
    void onSaveTextClicked();

private:
    void loadSettings();
    void saveSettings();
    void checkModelExists();
    void showModelNotFoundWarning();
    bool checkComponentsBeforeRecording();
    void addLog(const QString &message, const QString &level = "INFO");
    void applyAudioSettings();
    void startRealtime();
    void applyHotkey();
    void deliverDictation(const QString &text);
    QString vadModelIfEnabled() const;
    QString recordingsDirectory() const;

private:
    Ui::MainWindow *ui;
    SpeechRecognizer *m_recognizer;
    RealtimeRecognizer *m_realtime;
    GlobalHotkey *m_hotkey;
    QString m_hotkeyName;
    bool m_useVad;
    bool m_dictationStarted;
    bool m_isRecording;
    QTimer *m_recordingTimer;
    QElapsedTimer m_elapsedTimer;
};

#endif // MAINWINDOW_H
