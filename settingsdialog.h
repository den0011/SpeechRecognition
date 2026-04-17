#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    QString getWhisperPath() const;
    QString getModelPath() const;
    QString getLanguage() const;
    QString getAudioDevice() const;
    QString getPerformanceMode() const;

    void setWhisperPath(const QString &path);
    void setModelPath(const QString &path);
    void setLanguage(const QString &language);
    void setAudioDevice(const QString &device);
    void setPerformanceMode(const QString &mode);
    
    void setAvailableAudioDevices(const QStringList &devices);

signals:
    void refreshAudioDevices();

private slots:
    void onBrowseWhisperClicked();
    void onBrowseModelClicked();
    void onSaveClicked();
    void onCancelClicked();
    void onRefreshDevicesClicked();

private:
    Ui::SettingsDialog *ui;
};

#endif // SETTINGSDIALOG_H
