#pragma once

#include <QObject>
#include <QStringList>

// Open and save dialogs, behind an interface so tests can inject a fake.
// Results arrive asynchronously through the signals; `tag` identifies the
// request so the caller knows what the file is for.
class FilePicker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    struct Filter {
        QString name;
        QStringList patterns; // "*.csv"
    };

    virtual void openFile(const QString &tag, const QString &title, const QList<Filter> &filters,
                          const QString &folder) = 0;
    virtual void saveFile(const QString &tag, const QString &title, const QString &suggestedName,
                          const QList<Filter> &filters, const QString &folder) = 0;

signals:
    void selected(const QString &tag, const QString &path);
    void failed(const QString &tag, const QString &message);
};

#ifdef AMAT_PORTAL
// xdg-desktop-portal's FileChooser over D-Bus, so the dialog is the
// desktop's own and works the same in every Omarchy setup.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);
    void openFile(const QString &tag, const QString &title, const QList<Filter> &filters, const QString &folder) override;
    void saveFile(const QString &tag, const QString &title, const QString &suggestedName, const QList<Filter> &filters,
                  const QString &folder) override;

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    void request(const QString &method, const QString &tag, const QString &title, QVariantMap options,
                 const QList<Filter> &filters, const QString &folder);
    QString m_tag, m_handle;
    int m_counter = 0;
};
#else
// The platform's own dialog (Windows Explorer's), for systems without the
// portal. Needs QApplication, which main.cpp creates on those systems.
class NativeFilePicker : public FilePicker {
    Q_OBJECT

public:
    using FilePicker::FilePicker;
    void openFile(const QString &tag, const QString &title, const QList<Filter> &filters, const QString &folder) override;
    void saveFile(const QString &tag, const QString &title, const QString &suggestedName, const QList<Filter> &filters,
                  const QString &folder) override;
};
#endif
