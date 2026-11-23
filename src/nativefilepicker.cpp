#ifndef AMAT_PORTAL
#include "filepicker.h"

#include <QDir>
#include <QFileDialog>
#include <QTimer>

namespace {
QString nameFilters(const QList<FilePicker::Filter> &filters) {
    QStringList parts;
    for (const auto &f : filters) parts << QString("%1 (%2)").arg(f.name, f.patterns.join(' '));
    return parts.join(";;");
}
} // namespace

// The static dialogs are modal; run them from the event loop so the call
// returns first, like the asynchronous portal.
void NativeFilePicker::openFile(const QString &tag, const QString &title, const QList<Filter> &filters, const QString &folder) {
    QTimer::singleShot(0, this, [this, tag, title, filters, folder] {
        const QString path = QFileDialog::getOpenFileName(nullptr, title, QDir::toNativeSeparators(folder), nameFilters(filters));
        if (!path.isEmpty()) emit selected(tag, path);
    });
}

void NativeFilePicker::saveFile(const QString &tag, const QString &title, const QString &suggestedName,
                                const QList<Filter> &filters, const QString &folder) {
    QTimer::singleShot(0, this, [this, tag, title, suggestedName, filters, folder] {
        const QString path = QFileDialog::getSaveFileName(nullptr, title, QDir(folder).filePath(suggestedName), nameFilters(filters));
        if (!path.isEmpty()) emit selected(tag, path);
    });
}
#endif
