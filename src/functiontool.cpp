#include "tools.h"

#include <cmath>

FunctionTool::FunctionTool(QObject *parent) : Tool(parent) {
    setDefaults({{"f1", "0.1*x**3 + 0.75*x**2 - 1.5*x - 2"}, {"f2", ""}, {"f3", ""}, {"f4", ""},
                 {"xmin", "-10"}, {"xmax", "5"}, {"x0", ""}, {"logx", false}, {"logy", false},
                 {"roots", true}, {"extrema", true}, {"integral", true}, {"derivative", false}});
}

void FunctionTool::compute() {
    const double a = number("xmin", tr("x from")), b = number("xmax", tr("x to"));
    const bool logX = flag("logx"), logY = flag("logy");
    if (!m_pendingError.isEmpty()) {
        m_figure.clear(m_pendingError);
        return;
    }
    if (!(b > a)) {
        fail(tr("The range needs x from < x to"));
        m_figure.clear(m_pendingError);
        return;
    }
    if (logX && a <= 0) {
        fail(tr("A logarithmic x axis needs x from > 0"));
        m_figure.clear(m_pendingError);
        return;
    }
    const int samples = 2000;
    num::Vec xs(samples + 1);
    for (int i = 0; i <= samples; ++i)
        xs[size_t(i)] = logX ? a * std::pow(b / a, double(i) / samples) : a + (b - a) * i / samples;

    PlotAxes axes;
    axes.xLabel = "x";
    axes.yLabel = "f(x)";
    axes.logX = logX;
    axes.logY = logY;
    axes.lines.push_back({PlotLine::Horizontal, 0, 0, QColor(128, 128, 128, 150), {}, false});
    PlotSeries rootMarks, minMarks, maxMarks;
    rootMarks.style = minMarks.style = maxMarks.style = PlotSeries::Markers;
    rootMarks.markerSize = minMarks.markerSize = maxMarks.markerSize = 8;
    rootMarks.color = QColor("#ff6b6b");
    minMarks.color = QColor("#7bd88f");
    maxMarks.color = QColor("#c792ea");
    rootMarks.label = tr("Roots");
    minMarks.label = tr("Minima");
    maxMarks.label = tr("Maxima");

    const double x0 = number("x0", "x₀");
    QStringList code{"import numpy as np", "import matplotlib.pyplot as plt", "from scipy import optimize, integrate", ""};
    QStringList plotCode;
    plotCode << (logX ? QString("x = np.logspace(np.log10(%1), np.log10(%2), 1000)").arg(pyNumber(a), pyNumber(b))
                      : QString("x = np.linspace(%1, %2, 1000)").arg(pyNumber(a), pyNumber(b)))
             << "plt.figure(figsize=(10, 4))";
    int count = 0;
    for (int k = 1; k <= 4; ++k) {
        const QString source = text(QString("f%1").arg(k)).trimmed();
        if (source.isEmpty()) continue;
        QString error;
        const Expression e = Expression::parse(source, {"x"}, &error);
        const QString name = QString("f%1").arg(k);
        if (!e.isValid()) {
            fail(QString("%1: %2").arg(name, error));
            continue;
        }
        ++count;
        auto f = [&](double x) { return e.eval(&x); };
        PlotSeries s;
        s.label = name;
        s.x = xs;
        s.y.resize(xs.size());
        for (size_t i = 0; i < xs.size(); ++i) s.y[i] = f(xs[i]);
        axes.series.push_back(s);
        code << QString("def %1(x):\n    return %2\n").arg(name, e.toPython());
        plotCode << QString("plt.plot(x, %1(x), label='%1')").arg(name);

        heading(QString("%1(x) = %2").arg(name, source));
        if (!std::isnan(x0)) value(QString("%1(%2)").arg(name, fmt(x0)), f(x0));

        const Expression d = e.derivative(0);
        if (d.isValid()) {
            value(QString("%1'(x)").arg(name), d.toPython(), d.toPython());
            if (!std::isnan(x0)) value(QString("%1'(%2)").arg(name, fmt(x0)), d.eval(&x0));
            if (flag("derivative")) {
                PlotSeries ds;
                ds.label = name + "'";
                ds.dashed = true;
                ds.x = xs;
                ds.y.resize(xs.size());
                for (size_t i = 0; i < xs.size(); ++i) ds.y[i] = d.eval(&xs[i]);
                axes.series.push_back(ds);
                code << QString("def %1_ableitung(x):\n    return %2\n").arg(name, d.toPython());
                plotCode << QString("plt.plot(x, %1_ableitung(x), '--', label=\"%1'\")").arg(name);
            }
        }

        if (flag("roots")) {
            const num::Vec r = num::roots(f, a, b);
            QStringList list;
            for (double v : r) {
                list << fmt(v, 8);
                rootMarks.x.push_back(v);
                rootMarks.y.push_back(0);
            }
            value(tr("Roots"), list.isEmpty() ? tr("none in range") : list.join(";  "));
            for (double v : r)
                code << QString("print(optimize.root_scalar(%1, x0=%2).root)  # %3").arg(name, pyNumber(std::round(v * 100) / 100), fmt(v, 8));
        }
        if (flag("extrema")) {
            const auto ex = num::extrema(f, a, b);
            QStringList mins, maxs;
            const double w = (b - a) / 40;
            for (const auto &p : ex) {
                const QString item = QString("x = %1, y = %2").arg(fmt(p.x, 8), fmt(p.y, 8));
                (p.maximum ? maxs : mins) << item;
                (p.maximum ? maxMarks : minMarks).x.push_back(p.x);
                (p.maximum ? maxMarks : minMarks).y.push_back(p.y);
                const QString bounds = QString("(%1, %2)").arg(pyNumber(std::round((p.x - w) * 1000) / 1000), pyNumber(std::round((p.x + w) * 1000) / 1000));
                if (p.maximum)
                    code << QString("res = optimize.minimize_scalar(lambda x: -%1(x), bounds=%2, method='bounded')\nprint('Maximum', res.x, %1(res.x))").arg(name, bounds);
                else
                    code << QString("res = optimize.minimize_scalar(%1, bounds=%2, method='bounded')\nprint('Minimum', res.x, res.fun)").arg(name, bounds);
            }
            value(tr("Local minima"), mins.isEmpty() ? tr("none in range") : mins.join("\n"));
            value(tr("Local maxima"), maxs.isEmpty() ? tr("none in range") : maxs.join("\n"));
        }
        if (flag("integral")) {
            const double area = num::integrate(f, a, b);
            value(QString("∫ %1(x) dx  [%2, %3]").arg(name, fmt(a), fmt(b)), area);
            code << QString("print(integrate.quad(%1, %2, %3)[0])  # %4").arg(name, pyNumber(a), pyNumber(b), fmt(area, 8));
        }
    }
    if (!rootMarks.x.empty()) axes.series.push_back(rootMarks);
    if (!minMarks.x.empty()) axes.series.push_back(minMarks);
    if (!maxMarks.x.empty()) axes.series.push_back(maxMarks);
    if (!std::isnan(x0)) axes.lines.push_back({PlotLine::Vertical, x0, 0, QColor(200, 200, 200, 120), {}, true});
    if (count == 0) {
        if (m_pendingError.isEmpty()) fail(tr("Enter a function of x, e.g. 2*sin(x) + x**2"));
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    // Keep the view on the interesting part when a function shoots off.
    if (!logY) {
        num::Vec all;
        for (const auto &s : axes.series)
            for (double v : s.y)
                if (std::isfinite(v)) all.push_back(v);
        if (all.size() > 10) {
            const double lo = num::percentile(all, 1), hi = num::percentile(all, 99);
            const double mn = num::minimum(all), mx = num::maximum(all);
            if ((mx - mn) > 20 * (hi - lo) && hi > lo) {
                axes.hasYLimits = true;
                axes.yMin = lo - (hi - lo) * 0.3;
                axes.yMax = hi + (hi - lo) * 0.3;
            }
        }
    }
    m_figure.set({axes});
    if (logX) plotCode << "plt.xscale('log')";
    if (logY) plotCode << "plt.yscale('log')";
    plotCode << "plt.axhline(0, color='k', lw=0.8)" << "plt.xlabel('x')" << "plt.ylabel('f(x)')" << "plt.grid()"
             << "plt.legend()" << "plt.show()";
    publish(code.join('\n') + "\n\n" + plotCode.join('\n') + '\n');
}
