#pragma once

#include "filepicker.h"
#include "tools.h"

#include <QObject>
#include <QTimer>

class FigureView;

// Owns the pages' tools, remembers the session (every input survives a
// restart, so nothing is ever unsaved), and runs file dialogs and the
// clipboard for QML.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(Tool *calculator READ calculator CONSTANT)
    Q_PROPERTY(Tool *functions READ functions CONSTANT)
    Q_PROPERTY(Tool *vectors READ vectors CONSTANT)
    Q_PROPERTY(Tool *stats READ stats CONSTANT)
    Q_PROPERTY(Tool *propagation READ propagation CONSTANT)
    Q_PROPERTY(Tool *processing READ signalTool CONSTANT)
    Q_PROPERTY(Tool *spectrum READ spectrum CONSTANT)
    Q_PROPERTY(Tool *fit READ fit CONSTANT)
    Q_PROPERTY(Tool *table READ table CONSTANT)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    explicit Backend(FilePicker *picker = nullptr, const QString &sessionPath = {}, QObject *parent = nullptr);

    Tool *calculator() const { return m_calculator; }
    Tool *functions() const { return m_functions; }
    Tool *vectors() const { return m_vectors; }
    Tool *stats() const { return m_stats; }
    Tool *propagation() const { return m_propagation; }
    Tool *signalTool() const { return m_signal; }
    Tool *spectrum() const { return m_spectrum; }
    Tool *fit() const { return m_fit; }
    Tool *table() const { return m_table; }
    int page() const { return m_page; }
    void setPage(int page);
    QString message() const { return m_message; }

    Q_INVOKABLE void openData(Tool *tool);
    Q_INVOKABLE void openPath(Tool *tool, const QString &pathOrUrl);
    Q_INVOKABLE void exportFigure(QObject *figureView, const QString &suggestedName);
    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE void say(const QString &text);
    // The theory reference as rich text, German or English.
    Q_INVOKABLE QString reference(const QString &language, const QString &accent) const;
    void saveNow();

signals:
    void pageChanged();
    void messageChanged();

private:
    void load();
    void handleSelected(const QString &tag, const QString &path);
    void scheduleSave() { m_saveTimer.start(); }

    FilePicker *m_picker;
    QString m_sessionPath;
    Tool *m_calculator, *m_functions, *m_vectors, *m_stats, *m_propagation, *m_signal, *m_spectrum, *m_fit, *m_table;
    QList<Tool *> m_tools;
    int m_page = 0;
    QString m_message;
    QTimer m_saveTimer, m_messageTimer;
    Tool *m_pendingOpen = nullptr;
    FigureView *m_pendingExport = nullptr;
    QString m_lastFolder;
};
