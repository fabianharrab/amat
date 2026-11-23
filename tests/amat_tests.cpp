#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "backend.h"
#include "datasource.h"
#include "expression.h"
#include "numeric.h"
#include "theme.h"
#include "tools.h"
#include "translator.h"

using num::Vec;

namespace {
// x = sin(0.7 n) + 0.05 n, n = 0..59; reference values are SciPy 1.18's.
Vec testSignal() {
    Vec x(60);
    for (int i = 0; i < 60; ++i) x[size_t(i)] = std::sin(i * 0.7) + 0.05 * i;
    return x;
}

QString answerOf(Tool *tool) {
    tool->recompute();
    for (const auto &r : tool->results())
        if (r.toMap().value("kind") == "answer") return r.toMap().value("value").toString();
    return {};
}

class FakePicker : public FilePicker {
public:
    QString lastTag;
    void openFile(const QString &tag, const QString &, const QList<Filter> &, const QString &) override { lastTag = tag; }
    void saveFile(const QString &tag, const QString &, const QString &, const QList<Filter> &, const QString &) override { lastTag = tag; }
};
} // namespace

class AmatTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QLocale::setDefault(QLocale::c()); }

    void expressionsFollowPython()
    {
        QCOMPARE(evalNumber("-2**2"), -4.0);
        QCOMPARE(evalNumber("2**3**2"), 512.0);
        QCOMPARE(evalNumber("2^-1"), 0.5);
        QCOMPARE(evalNumber("7 % 3"), 1.0);
        QCOMPARE(evalNumber("-7 % 3"), 2.0);
        QVERIFY(std::abs(evalNumber("2pi") - 2 * M_PI) < 1e-15);
        QVERIFY(std::abs(evalNumber("np.degrees(np.arctan2(3.5, 2.1))") - 59.0362434679) < 1e-9);
        QVERIFY(std::abs(evalNumber("constants.g") - 9.80665) < 1e-15);
        bool ok = true;
        QString error;
        evalNumber("foo(2)", &ok, 0, &error);
        QVERIFY(!ok);
        QVERIFY(error.contains("foo"));
    }

    void derivativesReadLikeTheCourse()
    {
        const Expression g = Expression::parse("4*pi**2*L/T**2", {"L", "T"});
        QCOMPARE(g.derivative(0).toPython(), QString("4*np.pi**2/T**2"));
        QCOMPARE(g.derivative(1).toPython(), QString("-8*np.pi**2*L/T**3"));
        const Expression p = Expression::parse("R*I**2", {"R", "I"});
        QCOMPARE(p.derivative(0).toPython(), QString("I**2"));
        QCOMPARE(p.derivative(1).toPython(), QString("2*R*I"));
        QCOMPARE(Expression::freeNames("K_P*(1 - np.exp(-t/T_1)) + 1e-3"), QStringList({"K_P", "t", "T_1"}));
    }

    void butterworthMatchesScipy()
    {
        const Vec x = testSignal();
        Vec y = num::sosfiltfilt(num::butter(2, 1, 0, num::Band::Lowpass, 10), x);
        QVERIFY(std::abs(y[0] - -0.003223) < 1e-6);
        QVERIFY(std::abs(y[17] - 0.6116124) < 1e-6);
        QVERIFY(std::abs(y[59] - 2.50851087) < 1e-6);
        y = num::sosfiltfilt(num::butter(4, 1, 3, num::Band::Bandpass, 10), x);
        QVERIFY(std::abs(y[0] - 0.00438567) < 1e-6);
        QVERIFY(std::abs(y[30] - 0.70157997) < 1e-6);
        QString error;
        QVERIFY(num::butter(4, 6, 0, num::Band::Lowpass, 10, &error).empty());
        QVERIFY(!error.isEmpty());
    }

    void smoothingMatchesScipy()
    {
        const Vec x = testSignal();
        Vec y = num::savgolFilter(x, 9, 3);
        QVERIFY(std::abs(y[0] - -0.07797546) < 1e-7);
        QVERIFY(std::abs(y[20] - 1.77317994) < 1e-7);
        y = num::savgolFilter(x, 10, 2);
        QVERIFY(std::abs(y[20] - 1.69548559) < 1e-7);
        QVERIFY(std::abs(y[59] - 3.00737847) < 1e-7);
        y = num::decimate(x, 4);
        QCOMPARE(int(y.size()), 15);
        QVERIFY(std::abs(y[7] - 1.48497338) < 1e-7);
        QCOMPARE(num::findPeaks(x, {NAN, NAN, 1.0}), std::vector<int>({2, 11, 20, 29, 38, 47, 56}));
        QCOMPARE(num::medfilt({1, 9, 2, 3, 4}, 3), Vec({1, 2, 3, 3, 3}));
    }

    void spectrumFollowsTheWorkflow()
    {
        // 3.5 + 2.5 sin(2π·25 t), fs = 100 Hz, 10 s: DC 3.5, A(25 Hz) = 2.5.
        const int n = 1000;
        Vec y(n);
        for (int i = 0; i < n; ++i) y[size_t(i)] = 3.5 + 2.5 * std::sin(2 * M_PI * 25 * i / 100.0);
        const auto Y = num::rfft(y);
        QVERIFY(std::abs(std::abs(Y[0]) / n - 3.5) < 1e-12);
        QVERIFY(std::abs(2 * std::abs(Y[250]) / n - 2.5) < 1e-12);
        QCOMPARE(num::rfftfreq(n, 0.01)[250], 25.0);
        const Vec w = num::window("hann", 4);
        QCOMPARE(w, Vec({0, 0.5, 1, 0.5})); // periodic, like get_window
    }

    void fitsAndInterpolation()
    {
        Vec x, y;
        for (int i = 0; i < 50; ++i) {
            x.push_back(i * 0.01);
            y.push_back(3 * (1 - std::exp(-x.back() / 0.15)));
        }
        const auto fit = num::curveFit([](double t, const Vec &p) { return p[0] * (1 - std::exp(-t / p[1])); }, x, y, {1, 1});
        QVERIFY(fit.ok);
        QVERIFY(std::abs(fit.params[0] - 3) < 1e-6);
        QVERIFY(std::abs(fit.params[1] - 0.15) < 1e-6);
        const auto r = num::linregress({1, 2, 3, 4}, {3, 5, 7, 9});
        QCOMPARE(r.slope, 2.0);
        QCOMPARE(r.intercept, 1.0);
        num::CubicSpline s;
        QVERIFY(s.fit({0, 1, 2, 3, 4}, {0, 1, 8, 27, 64}));
        QVERIFY(std::abs(s(2.5) - 15.625) < 1e-12); // not-a-knot reproduces a cubic
        QCOMPARE(num::interp(1.5, {1, 2}, {10, 20}), 15.0);
        const Vec roots = num::roots([](double v) { return 0.1 * v * v * v + 0.75 * v * v - 1.5 * v - 2; }, -10, 10);
        QCOMPARE(int(roots.size()), 3);
        QVERIFY(std::abs(roots[2] - 2.372969680465611) < 1e-12);
    }

    void uncertaintyRounding()
    {
        QCOMPARE(num::uncertaintyDecimals(0.01364), 3);
        QCOMPARE(num::uncertaintyDecimals(0.354), 2);
        QCOMPARE(num::uncertaintyDecimals(49.2), 0);
        QCOMPARE(num::uncertaintyDecimals(0.0996), 2); // rounds up to 0.10
    }

    void statisticsGivesTheCourseResult()
    {
        StatsTool stats;
        QCOMPARE(answerOf(&stats), QString("n = 1230 ± 49 rpm  (k = 2)"));
        stats.source()->setPasteText("0.85\n0.83\n0.86\n0.88");
        stats.set("name", "I");
        stats.set("unit", "A");
        stats.set("u", "0.001");
        QVERIFY(answerOf(&stats).startsWith("I = 0.855 ± 0.021 A"));
    }

    void propagationGivesTheCourseResult()
    {
        PropagationTool tool;
        QCOMPARE(answerOf(&tool), QString("g = 10.04 ± 0.35 m/s²  (k = 2)"));
        QCOMPARE(tool.variables(), QStringList({"L", "T"}));
        tool.set("formula", "R*I**2");
        tool.set("name", "P");
        tool.set("unit", "W");
        tool.set("val:R", "12");
        tool.set("u:R", "0.6");
        tool.set("kind:R", "rect");
        tool.set("val:I", "0.8568614966");
        tool.set("u:I", "0.006821569858");
        QCOMPARE(answerOf(&tool), QString("P = 8.81 ± 0.58 W  (k = 2)"));
    }

    void signalStepsUndo()
    {
        SignalTool tool;
        const int before = int(tool.steps().size());
        tool.addStep("savgol");
        QCOMPARE(int(tool.steps().size()), before + 1);
        tool.undo();
        QCOMPARE(int(tool.steps().size()), before);
        tool.redo();
        QCOMPARE(int(tool.steps().size()), before + 1);
        tool.recompute();
        QVERIFY(tool.error().isEmpty());
        QVERIFY(tool.code().contains("signal.savgol_filter(y, window_length=9, polyorder=3)"));
    }

    void dataFilesLikeLoadtxt()
    {
        QString error;
        DataTable t = DataTable::parseText("\xEF\xBB\xBFt [s];y [V]\n0;1,5\n0,016;2,5\n", &error);
        QCOMPARE(t.names, QStringList({"t [s]", "y [V]"}));
        QCOMPARE(t.columns[1], Vec({1.5, 2.5}));
        QCOMPARE(t.skipRows, 1);
        t = DataTable::parseText("# X-Beschleunigung [g]\n# (Abtastrate 5120 Hz)\n1e-3\n2e-3\n", &error);
        QCOMPARE(t.names, QStringList({"X-Beschleunigung [g]"}));
        QCOMPARE(t.sampleRate, 5120.0);
        t = DataTable::parseText("Zeit;Hoehe\nt;h\n[s];[m]\n0;1\n1;2\n", &error);
        QCOMPARE(t.names, QStringList({"Zeit t [s]", "Hoehe h [m]"}));

        // A 16-bit mono WAV with two samples.
        QByteArray wav("RIFF");
        auto u32 = [&](quint32 v) { wav.append(reinterpret_cast<const char *>(&v), 4); };
        auto u16 = [&](quint16 v) { wav.append(reinterpret_cast<const char *>(&v), 2); };
        u32(40); wav += "WAVEfmt "; u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
        wav += "data"; u32(4); u16(quint16(1000)); u16(quint16(-1000));
        t = DataTable::readWav(wav, &error);
        QCOMPARE(t.sampleRate, 8000.0);
        QCOMPARE(t.columns[0], Vec({1000, -1000}));
    }

    void sessionSurvivesRestart()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("session.json");
        {
            FakePicker picker;
            Backend backend(&picker, path);
            backend.functions()->set("f1", "x**2 - 2");
            static_cast<CalculatorTool *>(backend.calculator())->evaluate("a = 21*2");
            backend.setPage(3);
            backend.openData(backend.stats());
            QCOMPARE(picker.lastTag, QString("open"));
            backend.saveNow();
        }
        FakePicker picker;
        Backend backend(&picker, path);
        QCOMPARE(backend.functions()->inputs().value("f1").toString(), QString("x**2 - 2"));
        QCOMPARE(backend.page(), 3);
        QCOMPARE(static_cast<CalculatorTool *>(backend.calculator())->evaluate("a/2"), QString("21"));
    }

    void arraysComputeColumnsRowByRow()
    {
        TableTool tool;
        tool.recompute();
        QVERIFY2(tool.error().isEmpty(), qPrintable(tool.error()));
        QCOMPARE(tool.columns(), QStringList({"t", "phi", "delta_r", "L", "x", "y"}));
        bool found = false;
        for (const auto &r : tool.results()) {
            const auto m = r.toMap();
            if (m.value("label") == "x[0]") {
                QCOMPARE(m.value("copy").toString(), QString("0.6"));
                found = true;
            }
        }
        QVERIFY(found);
        QVERIFY(tool.code().contains("x = L*np.cos(phi)"));
        tool.set("formulas", "x = ");
        tool.recompute();
        QVERIFY(!tool.error().isEmpty());
    }

    void rotationMatrixCheck()
    {
        VectorTool tool;
        tool.set("m", "0.54030231, -0.84147098; 0.84147098, 0.54030231");
        QVERIFY(answerOf(&tool).startsWith("Yes"));
        tool.set("m", "0.54030231, -0.84147098; 1.14147098, 0.54030231");
        QVERIFY(answerOf(&tool).startsWith("No"));
        tool.set("m", "0.54, -0.84, 0.84; 0.84, 0.54, 0.54");
        QCOMPARE(answerOf(&tool), QString("No: not square"));
    }

    void statisticsFromSummaryValues()
    {
        StatsTool tool;
        tool.set("summary", true);
        tool.set("name", "m");
        tool.set("unit", "g");
        QCOMPARE(answerOf(&tool), QString("m = 248.70 ± 0.56 g  (k = 2)"));
    }

    void spectrumPeaksAboveHeight()
    {
        SpectrumTool tool;
        tool.source()->setFormula("3 + 7*sin(2*pi*25*t) + 2*sin(2*pi*37*t)");
        tool.set("height", "5");
        QCOMPARE(answerOf(&tool), QString("25.000"));
        QVERIFY(tool.code().contains("signal.find_peaks(A, height=5)"));
    }

    void svgExport()
    {
        QTemporaryDir dir;
        Figure figure;
        PlotAxes axes;
        axes.series.push_back(PlotSeries{"y", QColor(), PlotSeries::Line, {0, 1, 2}, {0, 1, 4}});
        figure.set({axes});
        QString error;
        QVERIFY(figure.exportImage(dir.filePath("plot.svg"), {}, &error));
        QFile f(dir.filePath("plot.svg"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("<svg"));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
    }

    void germanCatalogCoversTheInterface()
    {
        // Every catalog entry must be valid JSON and the German one complete
        // enough to be worth shipping.
        QFile de(":/i18n/de.json");
        QVERIFY(de.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(de.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(doc.object().size() > 150);
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(AmatTests)
#include "amat_tests.moc"
