#include "backend.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include "figure.h"

Backend::Backend(FilePicker *picker, const QString &sessionPath, QObject *parent)
    : QObject(parent), m_picker(picker ? picker : new PortalFilePicker(this)),
      m_sessionPath(!sessionPath.isEmpty() ? sessionPath
                    : !qEnvironmentVariableIsEmpty("AMAT_SESSION") ? qEnvironmentVariable("AMAT_SESSION")
                    : QDir::homePath() + "/.local/state/amat/session.json") {
    m_calculator = new CalculatorTool(this);
    m_functions = new FunctionTool(this);
    m_vectors = new VectorTool(this);
    m_stats = new StatsTool(this);
    m_propagation = new PropagationTool(this);
    m_signal = new SignalTool(this);
    m_spectrum = new SpectrumTool(this);
    m_fit = new FitTool(this);
    m_table = new TableTool(this);
    m_tools = {m_calculator, m_functions, m_vectors, m_stats, m_propagation, m_signal, m_spectrum, m_fit, m_table};

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &Backend::saveNow);
    m_messageTimer.setSingleShot(true);
    m_messageTimer.setInterval(5000);
    connect(&m_messageTimer, &QTimer::timeout, this, [this] {
        m_message.clear();
        emit messageChanged();
    });

    connect(m_picker, &FilePicker::selected, this, &Backend::handleSelected);
    connect(m_picker, &FilePicker::failed, this, [this](const QString &, const QString &message) { say(message); });

    load();
    for (Tool *tool : m_tools) connect(tool, &Tool::stateChanged, this, &Backend::scheduleSave);
}

void Backend::setPage(int page) {
    if (page == m_page) return;
    m_page = page;
    emit pageChanged();
    scheduleSave();
}

void Backend::say(const QString &text) {
    m_message = text;
    emit messageChanged();
    m_messageTimer.start();
}

QString Backend::reference(const QString &language, const QString &accent) const {
    QFile file(language.startsWith("de") ? ":/reference/de.html" : ":/reference/en.html");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).replace("ACCENT", accent);
}

void Backend::copy(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
    say(tr("Copied to the clipboard"));
}

void Backend::openData(Tool *tool) {
    if (!tool || !tool->source()) return;
    m_pendingOpen = tool;
    QString folder = m_lastFolder;
    if (folder.isEmpty() && tool->source()->mode() == "file") folder = QFileInfo(tool->source()->path()).absolutePath();
    if (folder.isEmpty()) folder = QDir::homePath() + "/Documents";
    m_picker->openFile("open", tr("Open data"),
                       {{tr("Data files"), {"*.csv", "*.txt", "*.TXT", "*.CSV", "*.dat", "*.wav", "*"}},
                        {tr("All files"), {"*"}}},
                       folder);
}

void Backend::openPath(Tool *tool, const QString &pathOrUrl) {
    if (!tool || !tool->source()) return;
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    tool->source()->setPath(path);
    if (!tool->source()->error().isEmpty()) say(tool->source()->error());
    else say(tr("Loaded %1").arg(QFileInfo(path).fileName()));
}

void Backend::exportFigure(QObject *figureView, const QString &suggestedName) {
    m_pendingExport = qobject_cast<FigureView *>(figureView);
    if (!m_pendingExport) return;
    m_picker->saveFile("export", tr("Export plot"), suggestedName + ".png", {{tr("PNG image"), {"*.png"}}, {tr("SVG vector image"), {"*.svg"}}},
                       m_lastFolder.isEmpty() ? QDir::homePath() + "/Documents" : m_lastFolder);
}

void Backend::handleSelected(const QString &tag, const QString &path) {
    m_lastFolder = QFileInfo(path).absolutePath();
    scheduleSave();
    if (tag == "open" && m_pendingOpen) openPath(m_pendingOpen, path);
    if (tag == "export" && m_pendingExport) {
        QString target = path;
        const QString suffix = QFileInfo(target).suffix().toLower();
        if (suffix != "png" && suffix != "jpg" && suffix != "jpeg" && suffix != "svg") target += ".png";
        const QString error = m_pendingExport->exportImage(target);
        say(error.isEmpty() ? tr("Saved %1").arg(QFileInfo(target).fileName()) : tr("Could not save: %1").arg(error));
    }
}

void Backend::load() {
    QFile file(m_sessionPath);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QVariantMap s = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
    const QStringList keys{"calculator", "functions", "vectors", "stats", "propagation", "signal", "spectrum", "fit", "table"};
    for (int i = 0; i < keys.size(); ++i)
        if (s.contains(keys[i])) m_tools[i]->setState(s.value(keys[i]).toMap());
    m_page = s.value("page", 0).toInt();
    m_lastFolder = s.value("folder").toString();
}

void Backend::saveNow() {
    QVariantMap s;
    const QStringList keys{"calculator", "functions", "vectors", "stats", "propagation", "signal", "spectrum", "fit", "table"};
    for (int i = 0; i < keys.size(); ++i) s[keys[i]] = m_tools[i]->state();
    s["page"] = m_page;
    s["folder"] = m_lastFolder;
    QDir().mkpath(QFileInfo(m_sessionPath).absolutePath());
    // Written beside the session file and renamed into place.
    QSaveFile file(m_sessionPath);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument::fromVariant(s).toJson());
    file.commit();
}
