#ifdef AMAT_PORTAL
#include "filepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QUrl>

namespace {

// The portal's filter type: a(sa(us)), a list of (name, [(0, glob)]).
struct PortalRule {
    uint kind = 0;
    QString pattern;
};
struct PortalFilter {
    QString name;
    QList<PortalRule> rules;
};
using PortalFilters = QList<PortalFilter>;

} // namespace

Q_DECLARE_METATYPE(PortalRule)
Q_DECLARE_METATYPE(PortalFilter)
Q_DECLARE_METATYPE(PortalFilters)

QDBusArgument &operator<<(QDBusArgument &a, const PortalRule &r) {
    a.beginStructure();
    a << r.kind << r.pattern;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, PortalRule &r) {
    a.beginStructure();
    a >> r.kind >> r.pattern;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const PortalFilter &f) {
    a.beginStructure();
    a << f.name << f.rules;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, PortalFilter &f) {
    a.beginStructure();
    a >> f.name >> f.rules;
    a.endStructure();
    return a;
}

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {
    qDBusRegisterMetaType<PortalRule>();
    qDBusRegisterMetaType<QList<PortalRule>>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<PortalFilters>();
}

void PortalFilePicker::openFile(const QString &tag, const QString &title, const QList<Filter> &filters, const QString &folder) {
    request("OpenFile", tag, title, {}, filters, folder);
}

void PortalFilePicker::saveFile(const QString &tag, const QString &title, const QString &suggestedName,
                                const QList<Filter> &filters, const QString &folder) {
    request("SaveFile", tag, title, {{"current_name", suggestedName}}, filters, folder);
}

void PortalFilePicker::request(const QString &method, const QString &tag, const QString &title, QVariantMap options,
                               const QList<Filter> &filters, const QString &folder) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(tag, tr("No D-Bus session; the file dialog needs xdg-desktop-portal"));
        return;
    }
    // Subscribe to the predictable request path before calling, so a fast
    // reply is never missed.
    const QString token = QString("amat%1").arg(++m_counter);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString handle = QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    if (!m_handle.isEmpty())
        bus.disconnect("org.freedesktop.portal.Desktop", m_handle, "org.freedesktop.portal.Request", "Response", this,
                       SLOT(onResponse(uint, QVariantMap)));
    m_handle = handle;
    m_tag = tag;
    bus.connect("org.freedesktop.portal.Desktop", handle, "org.freedesktop.portal.Request", "Response", this,
                SLOT(onResponse(uint, QVariantMap)));

    options["handle_token"] = token;
    options["modal"] = true;
    if (!filters.isEmpty()) {
        PortalFilters list;
        for (const auto &f : filters) {
            PortalFilter pf{f.name, {}};
            for (const auto &p : f.patterns) pf.rules << PortalRule{0, p};
            list << pf;
        }
        options["filters"] = QVariant::fromValue(list);
    }
    if (!folder.isEmpty()) options["current_folder"] = folder.toUtf8() + '\0';

    QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                          "org.freedesktop.portal.FileChooser", method);
    message << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, tag](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError())
            emit failed(tag, tr("The file dialog is unavailable (%1). You can also drop a file onto the window.")
                                 .arg(reply.error().message()));
        w->deleteLater();
    });
}

void PortalFilePicker::onResponse(uint response, const QVariantMap &results) {
    const QString tag = m_tag;
    m_tag.clear();
    if (response != 0) return; // cancelled
    const QStringList uris = results.value("uris").toStringList();
    if (uris.isEmpty()) return;
    emit selected(tag, QUrl(uris.first()).toLocalFile());
}
#endif
