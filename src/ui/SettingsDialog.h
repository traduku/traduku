#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;

namespace lt {

class Settings;

/*!
 * API/model/language/behaviour configuration, editing a Settings object.
 * Loads on open, writes + saves on accept (Cancel discards). "Test API"
 * probes <api>/models to give quick feedback.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(Settings *settings, QWidget *parent = nullptr);

private slots:
    void accept() override;
    void restoreDefaults();
    void testApi();

private:
    void load();

    Settings *m_settings;
    QLineEdit *m_apiUrl;
    QLineEdit *m_apiKey;
    QLineEdit *m_model;
    QLineEdit *m_globalShortcut;
    QLabel *m_testResult;
    QNetworkAccessManager *m_nam;
};

} // namespace lt
