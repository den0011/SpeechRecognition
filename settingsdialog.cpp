#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    setWindowTitle("РќР°СЃС‚СЂРѕР№РєРё - Р Р°СЃРїРѕР·РЅР°РІР°РЅРёРµ СЂРµС‡Рё");
    setMinimumSize(650, 600);
    resize(650, 600);

    connect(ui->browseWhisperButton, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseWhisperClicked);
    connect(ui->browseModelButton, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseModelClicked);
    connect(ui->saveButton, &QPushButton::clicked,
            this, &SettingsDialog::onSaveClicked);
    connect(ui->cancelButton, &QPushButton::clicked,
            this, &SettingsDialog::onCancelClicked);
    connect(ui->refreshDevicesButton, &QPushButton::clicked,
            this, &SettingsDialog::onRefreshDevicesClicked);

    ui->whisperPathEdit->setToolTip("РџСѓС‚СЊ Рє РёСЃРїРѕР»РЅСЏРµРјРѕРјСѓ С„Р°Р№Р»Сѓ whisper.cpp/whisper-cli");
    ui->modelPathEdit->setToolTip("РџСѓС‚СЊ Рє С„Р°Р№Р»Сѓ РјРѕРґРµР»Рё whisper (.bin С„Р°Р№Р»)");
    ui->audioDeviceComboBox->setToolTip("Р’С‹Р±РµСЂРёС‚Рµ РјРёРєСЂРѕС„РѕРЅ РґР»СЏ Р·Р°РїРёСЃРё");
    ui->performanceModeComboBox->setItemData(0, "fast");
    ui->performanceModeComboBox->setItemData(1, "balanced");
    ui->performanceModeComboBox->setItemData(2, "accurate");
    ui->performanceModeComboBox->setCurrentIndex(1);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

QString SettingsDialog::getWhisperPath() const
{
    return ui->whisperPathEdit->text();
}

QString SettingsDialog::getModelPath() const
{
    return ui->modelPathEdit->text();
}

QString SettingsDialog::getLanguage() const
{
    return ui->languageComboBox->currentText();
}

QString SettingsDialog::getAudioDevice() const
{
    return ui->audioDeviceComboBox->currentText();
}

QString SettingsDialog::getPerformanceMode() const
{
    return ui->performanceModeComboBox->currentData().toString();
}

void SettingsDialog::setWhisperPath(const QString &path)
{
    ui->whisperPathEdit->setText(path);
}

void SettingsDialog::setModelPath(const QString &path)
{
    ui->modelPathEdit->setText(path);
}

void SettingsDialog::setLanguage(const QString &language)
{
    int index = ui->languageComboBox->findText(language);
    if (index != -1) {
        ui->languageComboBox->setCurrentIndex(index);
    }
}

void SettingsDialog::setAudioDevice(const QString &device)
{
    int index = ui->audioDeviceComboBox->findText(device);
    if (index != -1) {
        ui->audioDeviceComboBox->setCurrentIndex(index);
    }
}

void SettingsDialog::setPerformanceMode(const QString &mode)
{
    int index = ui->performanceModeComboBox->findData(mode);
    if (index != -1) {
        ui->performanceModeComboBox->setCurrentIndex(index);
    }
}

void SettingsDialog::setAvailableAudioDevices(const QStringList &devices)
{
    ui->audioDeviceComboBox->clear();

    if (devices.isEmpty()) {
        ui->audioDeviceComboBox->addItem("РЈСЃС‚СЂРѕР№СЃС‚РІР° РЅРµ РЅР°Р№РґРµРЅС‹");
        ui->audioDeviceComboBox->setEnabled(false);
    } else {
        ui->audioDeviceComboBox->addItems(devices);
        ui->audioDeviceComboBox->setEnabled(true);
    }
}

void SettingsDialog::onBrowseWhisperClicked()
{
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString fileName = QFileDialog::getOpenFileName(this,
        "Р’С‹Р±РµСЂРёС‚Рµ РёСЃРїРѕР»РЅСЏРµРјС‹Р№ С„Р°Р№Р» whisper",
        homeDir,
        "РСЃРїРѕР»РЅСЏРµРјС‹Рµ С„Р°Р№Р»С‹ (*);;Р’СЃРµ С„Р°Р№Р»С‹ (*.*)");

    if (!fileName.isEmpty()) {
        ui->whisperPathEdit->setText(fileName);
    }
}

void SettingsDialog::onBrowseModelClicked()
{
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString fileName = QFileDialog::getOpenFileName(this,
        "Р’С‹Р±РµСЂРёС‚Рµ С„Р°Р№Р» РјРѕРґРµР»Рё whisper",
        homeDir,
        "РњРѕРґРµР»Рё whisper (*.bin);;Р’СЃРµ С„Р°Р№Р»С‹ (*.*)");

    if (!fileName.isEmpty()) {
        ui->modelPathEdit->setText(fileName);
    }
}

void SettingsDialog::onSaveClicked()
{
    if (ui->whisperPathEdit->text().isEmpty()) {
        QMessageBox::warning(this, "РџСЂРµРґСѓРїСЂРµР¶РґРµРЅРёРµ",
            "РџРѕР¶Р°Р»СѓР№СЃС‚Р°, СѓРєР°Р¶РёС‚Рµ РїСѓС‚СЊ Рє whisper");
        return;
    }

    if (ui->modelPathEdit->text().isEmpty()) {
        QMessageBox::warning(this, "РџСЂРµРґСѓРїСЂРµР¶РґРµРЅРёРµ",
            "РџРѕР¶Р°Р»СѓР№СЃС‚Р°, СѓРєР°Р¶РёС‚Рµ РїСѓС‚СЊ Рє РјРѕРґРµР»Рё");
        return;
    }

    if (ui->audioDeviceComboBox->currentText() == "РЈСЃС‚СЂРѕР№СЃС‚РІР° РЅРµ РЅР°Р№РґРµРЅС‹") {
        QMessageBox::warning(this, "РџСЂРµРґСѓРїСЂРµР¶РґРµРЅРёРµ",
            "РќРµ РЅР°Р№РґРµРЅРѕ РЅРё РѕРґРЅРѕРіРѕ СѓСЃС‚СЂРѕР№СЃС‚РІР° Р·Р°РїРёСЃРё Р·РІСѓРєР°.\n"
            "РџРѕРґРєР»СЋС‡РёС‚Рµ РјРёРєСЂРѕС„РѕРЅ Рё РїРµСЂРµР·Р°РїСѓСЃС‚РёС‚Рµ РїСЂРѕРіСЂР°РјРјСѓ.");
        return;
    }

    accept();
}

void SettingsDialog::onCancelClicked()
{
    reject();
}

void SettingsDialog::onRefreshDevicesClicked()
{
    emit refreshAudioDevices();
}
