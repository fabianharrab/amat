#include "tools.h"

#include <cmath>

namespace {

// "0.857 ± 0.014" with U rounded to two significant digits and the value
// to the same decimal place (the course's rounding rule).
QString rounded(double value, double U, int *decimalsOut = nullptr) {
    const int decimals = num::uncertaintyDecimals(U);
    if (decimalsOut) *decimalsOut = decimals;
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    if (decimals >= 0)
        return locale.toString(value, 'f', decimals) + " ± " + locale.toString(U, 'f', decimals);
    const double step = std::pow(10.0, -decimals);
    return locale.toString(std::round(value / step) * step, 'f', 0) + " ± " + locale.toString(std::round(U / step) * step, 'f', 0);
}

} // namespace

QString roundedResult(double value, double U, int *decimals) { return rounded(value, U, decimals); }


StatsTool::StatsTool(QObject *parent) : Tool(parent) {
    setSource(new DataSource(false, this));
    m_source->setPasteText("1072\n1104\n1288\n1239\n1276\n1290\n1160\n1223\n1171\n1173\n1178\n1236\n1413\n1401\n1224");
    setDefaults({{"name", "n"}, {"unit", "rpm"}, {"k", "2"}, {"u", ""}, {"a", ""}, {"bins", "10"}, {"summary", false}, {"sn", "100"}, {"smean", "248.7"}, {"ss", "2.8"}});
}

void StatsTool::compute() {
    if (flag("summary")) {
        computeSummary();
        return;
    }
    if (!m_source->error().isEmpty() || m_source->y().empty()) {
        fail(m_source->error().isEmpty() ? tr("No data") : m_source->error());
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    const double k = number("k", "k", 2);
    QString listError;
    const num::Vec extra = evalList(text("u"), &listError);
    if (!listError.isEmpty()) fail(tr("Further uncertainties: %1").arg(listError));
    const num::Vec halfWidths = evalList(text("a"), &listError);
    if (!listError.isEmpty()) fail(tr("Tolerances: %1").arg(listError));
    const QString unit = text("unit").trimmed();
    const QString name = text("name").trimmed().isEmpty() ? "x" : text("name").trimmed();

    struct Series {
        QString label, var;
        num::Vec data;
    };
    // Pasted numbers have no column name; call them by the symbol.
    const bool pasted = m_source->mode() == "paste";
    std::vector<Series> series{{pasted && m_source->y2().empty() ? name : m_source->yLabel(), "x", m_source->y()}};
    if (!m_source->y2().empty()) {
        series[0].var = "x1";
        series.push_back({m_source->y2Label(), "x2", m_source->y2()});
    }

    QStringList code{"import numpy as np", "import matplotlib.pyplot as plt", ""};
    code << m_source->python("i", series[0].var, series.size() > 1 ? "x2" : QString());

    PlotAxes scatter, hist, box;
    scatter.row = 0;
    scatter.columnSpan = 2;
    scatter.xLabel = tr("Index of the measurement");
    scatter.yLabel = withUnit(name, unit.isEmpty() ? QString() : "[" + unit + "]");
    hist.row = 1;
    hist.column = 0;
    hist.title = tr("Histogram");
    hist.yLabel = tr("Density");
    box.row = 1;
    box.column = 1;
    box.title = tr("Boxplot");

    const QColor colors[] = {QColor(), QColor("#5ac8fa")};
    int binCount = 10;
    for (size_t si = 0; si < series.size(); ++si) {
        const Series &s = series[si];
        const num::Vec &x = s.data;
        const int n = int(x.size());
        const double mean = num::mean(x), sd = num::stddev(x, 1), sem = sd / std::sqrt(double(n));
        const QString v = s.var;
        heading(series.size() > 1 ? s.label : tr("Measurement series (%1)").arg(s.label));
        value("N", QString::number(n), QString::number(n));
        value(tr("Mean x̄"), mean, unit);
        value(tr("Standard deviation s (ddof=1)"), sd, unit);
        value(tr("Standard deviation of the mean s/√N"), sem, unit);
        value(tr("Median"), num::median(x), unit);
        value(tr("Quartiles Q1 / Q3"), withUnit(fmt(num::percentile(x, 25)) + " / " + fmt(num::percentile(x, 75)), unit));
        value(tr("Min / Max"), withUnit(fmt(num::minimum(x)) + " / " + fmt(num::maximum(x)), unit));
        value(tr("95% band x̄ ± 2s"), withUnit(fmt(mean - 2 * sd) + " … " + fmt(mean + 2 * sd), unit));
        value(tr("95% confidence interval x̄ ± 2s/√N"), withUnit(fmt(mean) + " ± " + fmt(2 * sem), unit));

        code << "" << QString("N = len(%1)").arg(v) << QString("x_bar = %1.mean()  # Mittelwert").arg(v)
             << QString("s = %1.std(ddof=1)  # Standardabweichung (Stichprobe)").arg(v)
             << "u_1 = s / np.sqrt(N)  # Standardabweichung des Mittelwertes = Standardunsicherheit"
             << QString("print(N, x_bar, s, u_1, np.median(%1))").arg(v);

        // Uncertainty budget: statistics plus further standard uncertainties
        // and tolerances (rectangular distribution, u = a/√3), added in quadrature.
        double sumSquares = sem * sem;
        QStringList terms{"u_1**2"};
        int idx = 2;
        for (double u : extra) {
            value(tr("Further standard uncertainty u%1").arg(idx), u, unit);
            code << QString("u_%1 = %2  # weitere Standardunsicherheit").arg(idx).arg(pyNumber(u));
            terms << QString("u_%1**2").arg(idx++);
            sumSquares += u * u;
        }
        for (double a : halfWidths) {
            const double u = a / std::sqrt(3.0);
            value(tr("Tolerance ±%1 → u%2 = a/√3").arg(fmt(a)).arg(idx), u, unit);
            code << QString("u_%1 = %2 / np.sqrt(3)  # Rechteckverteilung, Halbbreite a").arg(idx).arg(pyNumber(a));
            terms << QString("u_%1**2").arg(idx++);
            sumSquares += u * u;
        }
        const double u = std::sqrt(sumSquares), U = k * u;
        value(tr("Combined standard uncertainty u"), u, unit);
        value(tr("Expanded uncertainty U = k·u"), U, unit);
        int decimals = 0;
        const QString result = QString("%1 = %2  (k = %3)").arg(name, withUnit(rounded(mean, U, &decimals), unit), fmt(k));
        answer(tr("Result"), result);
        value(tr("Relative uncertainty U/x̄"), fmt(100 * U / std::abs(mean), 3) + " %");
        code << QString("u = np.sqrt(%1)  # kombinierte Standardunsicherheit").arg(terms.join(" + "))
             << QString("k = %1  # Erweiterungsfaktor (95 %)").arg(pyNumber(k)) << "U = k * u  # erweiterte Messunsicherheit"
             << QString("print(f\"%1 = {x_bar:.%2f} +/- {U:.%2f} %3  (k = {k})\")").arg(name).arg(std::max(0, decimals)).arg(unit);

        PlotSeries pts;
        pts.style = PlotSeries::Markers;
        pts.label = series.size() > 1 ? s.label : tr("Measurements");
        pts.color = colors[si];
        pts.markerSize = n > 500 ? 3 : 6;
        for (int i = 0; i < n; ++i) {
            pts.x.push_back(i);
            pts.y.push_back(x[size_t(i)]);
        }
        scatter.series.push_back(pts);
        scatter.lines.push_back({PlotLine::Horizontal, mean, 0, QColor("#ff6b6b"), si == 0 ? tr("Mean") : QString(), false});
        scatter.lines.push_back({PlotLine::HorizontalSpan, mean - 2 * sd, mean + 2 * sd, QColor("#c792ea"),
                                 si == 0 ? tr("95% band (±2s)") : QString(), false});

        // Histogram (density) with the normal distribution estimated from x̄ and s.
        const QString binsText = text("bins").trimmed().toLower();
        binCount = binsText == "sqrt" || binsText == "√n" ? std::max(1, int(std::round(std::sqrt(double(n)))))
                                                          : std::clamp(int(evalNumber(binsText, nullptr, 10)), 1, 500);
        const auto h = num::histogram(x, binCount, true);
        PlotSeries bars;
        bars.style = PlotSeries::Bars;
        bars.color = colors[si];
        bars.opacity = series.size() > 1 ? 0.7 : 1;
        bars.barWidth = h.edges[1] - h.edges[0];
        for (int b = 0; b < binCount; ++b) {
            bars.x.push_back((h.edges[size_t(b)] + h.edges[size_t(b) + 1]) / 2);
            bars.y.push_back(h.counts[size_t(b)]);
        }
        hist.series.push_back(bars);
        PlotSeries pdf;
        pdf.color = si == 0 ? QColor("#ff6b6b") : QColor("#ffb86c");
        pdf.label = si == 0 ? tr("Normal distribution") : QString();
        for (int i = 0; i <= 200; ++i) {
            const double xv = mean - 3.5 * sd + 7 * sd * i / 200;
            pdf.x.push_back(xv);
            pdf.y.push_back(std::exp(-(xv - mean) * (xv - mean) / (2 * sd * sd)) / std::sqrt(2 * M_PI * sd * sd));
        }
        hist.series.push_back(pdf);
        box.boxes.push_back({s.label, num::boxStats(x), colors[si]});
        box.categories << s.label;
    }
    hist.xLabel = scatter.yLabel;
    box.yLabel = scatter.yLabel;
    m_figure.set({scatter, hist, box}, 2, 2);

    const QString v0 = series[0].var;
    code << "" << "fig, axs = plt.subplots(nrows=2, figsize=(10, 8))"
         << QString("axs[0].plot(%1, 'o', label='Messwerte')").arg(v0) << "axs[0].axhline(x_bar, color='r', label='Mittelwert')"
         << "axs[0].axhspan(x_bar - 2*s, x_bar + 2*s, color='m', alpha=0.1, label='95%-Band')"
         << "axs[0].set_xlabel('Index der Messung')" << "axs[0].legend()"
         << QString("axs[1].hist(%1, bins=%2, density=True)").arg(v0).arg(binCount)
         << "x_ = np.linspace(x_bar - 3*s, x_bar + 3*s, 200)"
         << "axs[1].plot(x_, np.exp(-np.square(x_ - x_bar)/(2*s**2)) / np.sqrt(2*np.pi*s**2), 'r')" << "plt.show()" << ""
         << QString("plt.boxplot(%1)").arg(series.size() > 1 ? "[x1, x2]" : v0) << "plt.show()";
    publish(code.join('\n') + '\n');
}

// Only N, x̄, and s are known (no raw data): the intervals and the result.
void StatsTool::computeSummary() {
    const double n = number("sn", "N"), mean = number("smean", tr("Mean x̄")), sd = number("ss", "s");
    const double k = number("k", "k", 2);
    const QString unit = text("unit").trimmed();
    const QString name = text("name").trimmed().isEmpty() ? "x" : text("name").trimmed();
    if (!m_pendingError.isEmpty() || !(n >= 1) || !(sd >= 0)) {
        fail(m_pendingError.isEmpty() ? tr("Enter N ≥ 1, the mean, and s ≥ 0") : m_pendingError);
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    const double sem = sd / std::sqrt(n);
    QString listError;
    double sumSquares = sem * sem;
    for (double u : evalList(text("u"), &listError)) sumSquares += u * u;
    for (double a : evalList(text("a"), &listError)) sumSquares += a * a / 3;
    const double u = std::sqrt(sumSquares), U = k * u;
    heading(tr("Single values (normal distribution)"));
    value(tr("≈ 68 % of the values in x̄ ± s"), withUnit(fmt(mean) + " ± " + fmt(sd), unit));
    value(tr("≈ 95 % of the values in x̄ ± 2s"), withUnit(fmt(mean) + " ± " + fmt(2 * sd), unit));
    value(tr("≈ 99.7 % of the values in x̄ ± 3s"), withUnit(fmt(mean) + " ± " + fmt(3 * sd), unit));
    heading(tr("Mean"));
    value(tr("Standard deviation of the mean s/√N"), sem, unit);
    value(tr("95% confidence interval x̄ ± 2s/√N"), withUnit(fmt(mean) + " ± " + fmt(2 * sem), unit));
    value(tr("Combined standard uncertainty u"), u, unit);
    value(tr("Expanded uncertainty U = k·u"), U, unit);
    int decimals = 0;
    answer(tr("Result"), QString("%1 = %2  (k = %3)").arg(name, withUnit(roundedResult(mean, U, &decimals), unit), fmt(k)));

    PlotAxes axes;
    axes.title = tr("Normal distribution from x̄ and s");
    axes.xLabel = withUnit(name, unit.isEmpty() ? QString() : "[" + unit + "]");
    axes.yLabel = tr("Density");
    PlotSeries pdf;
    pdf.width = 2;
    for (int i = 0; i <= 300; ++i) {
        const double x = mean - 4 * sd + 8 * sd * i / 300;
        pdf.x.push_back(x);
        pdf.y.push_back(sd > 0 ? std::exp(-(x - mean) * (x - mean) / (2 * sd * sd)) / std::sqrt(2 * M_PI * sd * sd) : 0);
    }
    axes.series.push_back(pdf);
    axes.lines.push_back({PlotLine::VerticalSpan, mean - 2 * sd, mean + 2 * sd, QColor("#c792ea"), tr("95% band (±2s)"), false});
    axes.lines.push_back({PlotLine::VerticalSpan, mean - 2 * sem, mean + 2 * sem, QColor("#ff6b6b"), tr("95% confidence interval of the mean"), false});
    axes.lines.push_back({PlotLine::Vertical, mean, 0, QColor("#ff6b6b"), tr("Mean"), false});
    m_figure.set({axes});
    publish(QString("import numpy as np\n\nN = %1\nx_bar = %2\ns = %3\nprint('95%%-Band der Einzelwerte:', x_bar, '+/-', 2*s)\n"
                    "print('95%%-Konfidenzintervall:', x_bar, '+/-', 2*s/np.sqrt(N))\n")
                .arg(pyNumber(n), pyNumber(mean), pyNumber(sd)));
}
