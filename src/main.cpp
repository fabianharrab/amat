// amat — calculations and plots for the Angewandte Mathematik module.

#include <QFileInfo>
#include <QGuiApplication>
#ifndef AMAT_PORTAL
#include <QApplication>
#endif
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "figure.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    if (isCliCommand(argc, argv))
        return runCli(argc, argv);

#ifdef AMAT_PORTAL
    QGuiApplication app(argc, argv);
#else
    // QFileDialog, used outside Linux, needs the widgets application.
    QApplication app(argc, argv);
#endif
    app.setApplicationName("amat");
    app.setOrganizationName("amat");
    app.setApplicationVersion("0.1.0");

    // Matches the window to amat.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("amat");
    app.setWindowIcon(QIcon::fromTheme("amat"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    // `amat FILE` or `amat open FILE` loads the file into the signal page
    // (WAV files into the spectrum page).
    QStringList args = app.arguments();
    args.removeFirst();
    if (!args.isEmpty() && args.first() == "open") args.removeFirst();
    if (!args.isEmpty() && QFileInfo::exists(args.first())) {
        const bool wav = args.first().endsWith(".wav", Qt::CaseInsensitive);
        backend.openPath(wav ? backend.spectrum() : backend.signalTool(), QFileInfo(args.first()).absoluteFilePath());
        backend.setPage(wav ? 6 : 5);
    }

    qmlRegisterType<FigureView>("Amat", 1, 0, "FigureView");
    qmlRegisterSingletonType(QUrl("qrc:/Style.qml"), "Amat", 1, 0, "Style");
    qmlRegisterUncreatableType<Figure>("Amat", 1, 0, "Figure", "Provided by tools");
    qmlRegisterUncreatableType<Tool>("Amat", 1, 0, "Tool", "Provided by the backend");
    qmlRegisterUncreatableType<DataSource>("Amat", 1, 0, "DataSource", "Provided by tools");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    const int code = app.exec();
    backend.saveNow();
    return code;
}
