#ifndef AUDIOCONFIGDIALOG_H
#define AUDIOCONFIGDIALOG_H

#include <QDialog>
#include <QAudioRecorder>
#include <QAudioProbe>
#include <QMediaPlayer>
#include <QTimer>
#include <QAudioBuffer>

namespace Ui {
class AudioConfigDialog;
}

class AudioConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AudioConfigDialog(QWidget *parent = nullptr);
    ~AudioConfigDialog();

    // Получение настроек
    int getSampleRate() const;
    int getBitRate() const;
    int getChannelCount() const;
    QString getAudioDevice() const;
    
    // Установка настроек
    void setSampleRate(int rate);
    void setBitRate(int rate);
    void setChannelCount(int channels);
    void setAudioDevice(const QString &device);
    void setAvailableAudioDevices(const QStringList &devices);

signals:
    void settingsSaved();

private slots:
    void onTestRecordClicked();
    void onTestPlayClicked();
    void onStopTestClicked();
    void onSaveClicked();
    void onCancelClicked();
    void onRefreshDevicesClicked();
    void updateAudioLevel();
    void onRecordingStateChanged();
    void onPlayerStateChanged();
    void onSavePresetClicked();
    void onLoadPresetClicked();
    void onDeletePresetClicked();
    void onPresetSelected(int index);

private:
    void setupAudioRecorder();
    void startLevelMonitoring();
    void stopLevelMonitoring();
    void updateVisualization(const QAudioBuffer &buffer);
    void saveTestRecording();
    void loadSettings();
    void applyEQSettings();
    void loadPresets();
    void savePresetToFile(const QString &presetName);
    void loadPresetFromFile(const QString &presetName);
    void deletePresetFromFile(const QString &presetName);
    QString getPresetsFilePath() const;

private:
    Ui::AudioConfigDialog *ui;
    QAudioRecorder *m_audioRecorder;
    QAudioProbe *m_audioProbe;
    QMediaPlayer *m_mediaPlayer;
    QTimer *m_levelTimer;
    QString m_testFilePath;
    qreal m_currentLevel;
    bool m_isTestRecording;
};

#endif // AUDIOCONFIGDIALOG_H
