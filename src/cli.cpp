// Commands that need no display, so scripts and agents can use the same
// math: `amat calc`, `amat stats`, `amat fft`, `amat plot`, `amat skill install`.
#include "cli.h"

#include "datasource.h"
#include "expression.h"
#include "figure.h"
#include "numeric.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <cstdio>

namespace {

const char *kSkill = R"(---
name: amat
description: Calculations for the Angewandte Mathematik module (NumPy/SciPy conventions) with the amat CLI - evaluate formulas, statistics with measurement uncertainty, FFT amplitude spectra, and function plots. Use when asked to compute these from the command line or check notebook results.
---

# amat

`amat` mirrors the NumPy/SciPy defaults the course uses. Every command takes
`--json`, exits 0 on success and 1 on error (message on stderr).

- `amat calc "4*pi**2*1.546/2.465**2"` evaluates a formula (np./math. prefixes allowed).
- `amat stats FILE [--column N] [--k 2] [--u 0.001,0.05] [--unit A]` prints N, mean,
  s (ddof=1), s/sqrt(N), combined u, U = k*u, and the rounded result.
- `amat fft FILE [--column N] [--fs HZ] [--window tukey] [--peaks 5]` prints the
  largest peaks of the amplitude spectrum (rfft/N, A[1:] *= 2).
- `amat plot "sin(x)/x" --from -10 --to 10 --out plot.png` writes a PNG.
- `amat open [FILE]` (or plain `amat`) opens the app.
)";

void printJson(const QJsonObject &o) { std::printf("%s\n", QJsonDocument(o).toJson(QJsonDocument::Indented).constData()); }

int failWith(const QString &message) {
    std::fprintf(stderr, "amat: %s\n", qPrintable(message));
    return 1;
}

int calc(const QStringList &args) {
    QCommandLineParser p;
    p.addOption({"json", "JSON output"});
    p.addPositionalArgument("formula", "Formula");
    p.process(args);
    if (p.positionalArguments().isEmpty()) return failWith("calc needs a formula");
    QString error;
    const Expression e = Expression::parse(p.positionalArguments().join(' '), {}, &error);
    if (!e.isValid()) return failWith(error);
    const double v = e.eval();
    if (p.isSet("json")) printJson({{"value", v}, {"python", e.toPython()}});
    else std::printf("%s\n", qPrintable(pyNumber(v)));
    return 0;
}

bool loadColumn(QCommandLineParser &p, num::Vec *y, double *fs, QString *error) {
    if (p.positionalArguments().isEmpty()) {
        *error = "needs a data file";
        return false;
    }
    const DataTable t = DataTable::readFile(p.positionalArguments().first(), error);
    if (t.columns.empty()) return false;
    const int c = p.isSet("column") ? p.value("column").toInt() : int(t.columns.size()) - 1;
    if (c < 0 || c >= int(t.columns.size())) {
        *error = QString("column %1 does not exist (0..%2)").arg(c).arg(t.columns.size() - 1);
        return false;
    }
    *y = t.columns[size_t(c)];
    if (fs) {
        *fs = t.sampleRate;
        if (t.columns.size() > 1 && c != 0 && t.rows() > 1) *fs = 1 / (t.columns[0][1] - t.columns[0][0]);
        if (p.isSet("fs")) *fs = evalNumber(p.value("fs"));
    }
    return true;
}

int stats(const QStringList &args) {
    QCommandLineParser p;
    p.addOption({"json", "JSON output"});
    p.addOption({"column", "Column (0-based)", "n"});
    p.addOption({"k", "Coverage factor", "k", "2"});
    p.addOption({"u", "Further standard uncertainties, comma separated", "list"});
    p.addOption({"unit", "Unit", "unit"});
    p.addPositionalArgument("file", "Data file");
    p.process(args);
    num::Vec x;
    QString error;
    if (!loadColumn(p, &x, nullptr, &error)) return failWith(error);
    const double k = evalNumber(p.value("k"), nullptr, 2);
    const int n = int(x.size());
    const double mean = num::mean(x), s = num::stddev(x, 1), sem = s / std::sqrt(double(n));
    double sum = sem * sem;
    for (double u : evalList(p.value("u"))) sum += u * u;
    const double u = std::sqrt(sum), U = k * u;
    const int d = std::max(0, num::uncertaintyDecimals(U));
    const QString result = QString("%1 +/- %2 %3 (k = %4)").arg(mean, 0, 'f', d).arg(U, 0, 'f', d).arg(p.value("unit")).arg(pyNumber(k));
    if (p.isSet("json"))
        printJson({{"n", n}, {"mean", mean}, {"std", s}, {"sem", sem}, {"median", num::median(x)}, {"u", u}, {"U", U},
                   {"k", k}, {"result", result.simplified()}});
    else
        std::printf("N       %d\nmean    %.10g\ns       %.10g\ns/sqrtN %.10g\nmedian  %.10g\nu       %.10g\nU       %.10g\nresult  %s\n",
                    n, mean, s, sem, num::median(x), u, U, qPrintable(result.simplified()));
    return 0;
}

int fft(const QStringList &args) {
    QCommandLineParser p;
    p.addOption({"json", "JSON output"});
    p.addOption({"column", "Column (0-based)", "n"});
    p.addOption({"fs", "Sampling rate [Hz]", "hz"});
    p.addOption({"window", "Window (boxcar, hann, hamming, blackman, bartlett, flattop, tukey)", "name", "boxcar"});
    p.addOption({"peaks", "Number of peaks", "n", "5"});
    p.addPositionalArgument("file", "Data file");
    p.process(args);
    num::Vec y;
    double fs = NAN;
    QString error;
    if (!loadColumn(p, &y, &fs, &error)) return failWith(error);
    if (!(fs > 0)) return failWith("the sampling rate is unknown; pass --fs");
    const int N = int(y.size());
    const num::Vec w = num::window(p.value("window"), N);
    const double mean = num::mean(y);
    for (int i = 0; i < N; ++i) y[size_t(i)] = w[size_t(i)] * (y[size_t(i)] - mean) + mean;
    const auto Y = num::rfft(y);
    num::Vec A(Y.size());
    for (size_t i = 0; i < Y.size(); ++i) A[i] = std::abs(Y[i]) / N * (i ? 2 : 1);
    auto peaks = num::findPeaks(A, {});
    std::sort(peaks.begin(), peaks.end(), [&](int a, int b) { return A[size_t(a)] > A[size_t(b)]; });
    peaks.resize(std::min(peaks.size(), size_t(p.value("peaks").toInt())));
    QJsonArray list;
    for (int i : peaks) list.append(QJsonObject{{"f", i * fs / N}, {"A", A[size_t(i)]}});
    if (p.isSet("json")) printJson({{"n", N}, {"fs", fs}, {"df", fs / N}, {"dc", A[0]}, {"peaks", list}});
    else {
        std::printf("N %d  fs %g Hz  df %g Hz  DC %.6g\n", N, fs, fs / N, A[0]);
        for (int i : peaks) std::printf("f = %-12.6g A = %.6g\n", i * fs / N, A[size_t(i)]);
    }
    return 0;
}

int plot(const QStringList &args) {
    QCommandLineParser p;
    p.addOption({"json", "JSON output"});
    p.addOption({"from", "x from", "x", "-10"});
    p.addOption({"to", "x to", "x", "10"});
    p.addOption({"out", "Output PNG or SVG", "file"});
    p.addPositionalArgument("formula", "f(x)");
    p.process(args);
    if (p.positionalArguments().isEmpty() || !p.isSet("out")) return failWith("plot needs a formula and --out file.png");
    QString error;
    const Expression e = Expression::parse(p.positionalArguments().first(), {"x"}, &error);
    if (!e.isValid()) return failWith(error);
    const double a = evalNumber(p.value("from")), b = evalNumber(p.value("to"));
    PlotAxes axes;
    axes.xLabel = "x";
    axes.yLabel = "f(x)";
    PlotSeries s;
    s.label = p.positionalArguments().first();
    for (int i = 0; i <= 1000; ++i) {
        const double x = a + (b - a) * i / 1000;
        s.x.push_back(x);
        s.y.push_back(e.eval(&x));
    }
    axes.series.push_back(s);
    Figure f;
    f.set({axes});
    if (!f.exportImage(p.value("out"), {}, &error)) return failWith(error);
    if (p.isSet("json")) printJson({{"path", QFileInfo(p.value("out")).absoluteFilePath()}});
    return 0;
}

int skill(const QStringList &args) {
    if (args.size() < 3 || args[2] != "install") return failWith("usage: amat skill install");
    const QString path = QDir::homePath() + "/.claude/skills/amat/SKILL.md";
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return failWith(f.errorString());
    f.write(kSkill);
    if (!f.commit()) return failWith(f.errorString());
    if (args.contains("--json")) printJson({{"path", path}});
    else std::printf("Installed %s\n", qPrintable(path));
    return 0;
}

} // namespace

bool isCliCommand(int argc, char **argv) {
    if (argc < 2) return false;
    const QString c = QString::fromLocal8Bit(argv[1]);
    return c == "calc" || c == "stats" || c == "fft" || c == "plot" || c == "skill" || c == "--help" || c == "-h";
}

int runCli(int argc, char **argv) {
    // Headless: no window, but QPainter and fonts still work for `plot`.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName("amat");
    QStringList args = app.arguments();
    const QString command = args.value(1);
    args.removeAt(1);
    if (command == "calc") return calc(args);
    if (command == "stats") return stats(args);
    if (command == "fft") return fft(args);
    if (command == "plot") return plot(args);
    if (command == "skill") return skill(app.arguments());
    std::printf("usage: amat [open [FILE]] | calc FORMULA | stats FILE | fft FILE | plot FORMULA --out FILE | skill install\n"
                "Each command takes --json and --help.\n");
    return 0;
}
