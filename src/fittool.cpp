#include "tools.h"

#include <algorithm>
#include <cmath>
#include <numeric>

FitTool::FitTool(QObject *parent) : Tool(parent) {
    setSource(new DataSource(false, this));
    m_source->setPairs(true);
    m_source->setPasteText("0.05\t25.8\n0.075\t37.1\n0.1\t51.9\n0.125\t61.7\n0.15\t76.3\n0.175\t86.2\n0.2\t101.4");
    m_source->setXColumn(0);
    m_source->setYColumn(1);
    setDefaults({{"model", "linear"}, {"degree", "2"}, {"formula", "K_P*(1 - exp(-x/T_1))"}, {"p0", ""}, {"at", ""}});
}

void FitTool::compute() {
    const QString model = text("model");
    const QString formula = text("formula");
    QStringList params = Expression::freeNames(formula);
    params.removeAll("x");
    if (params != m_parameters) {
        m_parameters = params;
        emit parametersChanged();
    }
    if (!m_source->error().isEmpty() || m_source->y().empty()) {
        fail(m_source->error().isEmpty() ? tr("No data") : m_source->error());
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    num::Vec x = m_source->x(), y = m_source->y();
    const size_t n = std::min(x.size(), y.size());
    x.resize(n);
    y.resize(n);
    QString listError;
    const num::Vec at = evalList(text("at"), &listError);
    if (!listError.isEmpty()) fail(tr("Evaluate at: %1").arg(listError));

    QStringList code{"import numpy as np", "import matplotlib.pyplot as plt", "from scipy import stats, optimize, interpolate", ""};
    code << m_source->python("x", "y") << "";
    std::function<double(double)> curve;
    QString curveCode;
    heading(tr("Data: %n points", "", int(n)));

    auto rSquared = [&](const std::function<double(double)> &f) {
        const double my = num::mean(y);
        double ssRes = 0, ssTot = 0;
        for (size_t i = 0; i < n; ++i) {
            ssRes += (y[i] - f(x[i])) * (y[i] - f(x[i]));
            ssTot += (y[i] - my) * (y[i] - my);
        }
        return ssTot > 0 ? 1 - ssRes / ssTot : NAN;
    };

    if (model == "linear") {
        if (n < 2) fail(tr("A regression needs at least 2 points"));
        else {
            const auto r = num::linregress(x, y);
            heading(tr("Linear regression (stats.linregress)"));
            value(tr("Slope m"), QString("%1 ± %2").arg(fmt(r.slope, 7), fmt(r.slopeStderr, 3)), pyNumber(r.slope));
            value(tr("Intercept b"), QString("%1 ± %2").arg(fmt(r.intercept, 7), fmt(r.interceptStderr, 3)), pyNumber(r.intercept));
            value(tr("Correlation r"), r.r);
            value("R²", r.r * r.r);
            answer(tr("Equation"), QString("y = %1·x %2 %3").arg(fmt(r.slope), r.intercept < 0 ? "−" : "+", fmt(std::abs(r.intercept))));
            curve = [r](double v) { return r.slope * v + r.intercept; };
            code << "res = stats.linregress(x, y)" << "print(res.slope, res.intercept, res.rvalue**2)";
            curveCode = "y_fit = res.intercept + res.slope * x_fit";
        }
    } else if (model == "poly") {
        const int degree = std::clamp(int(number("degree", tr("Degree"), 2)), 0, 12);
        QString error;
        const num::Vec p = num::polyfit(x, y, degree, &error);
        if (p.empty()) fail(error);
        else {
            heading(tr("Polynomial of degree %1 (np.polyfit)").arg(degree));
            QStringList terms;
            for (int i = 0; i <= degree; ++i) {
                const int power = degree - i;
                value(power == 0 ? "a₀" : QString("a%1 (x^%1)").arg(power), p[size_t(i)]);
                terms << fmt(p[size_t(i)]) + (power == 0 ? "" : power == 1 ? "·x" : QString("·x^%1").arg(power));
            }
            curve = [p](double v) { return num::polyval(p, v); };
            value("R²", rSquared(curve));
            answer(tr("Equation"), "y = " + terms.join(" + ").replace("+ -", "− "));
            code << QString("p = np.polyfit(x, y, %1)  # Koeffizienten, hoechste Potenz zuerst").arg(degree) << "print(p)";
            curveCode = "y_fit = np.polyval(p, x_fit)";
        }
    } else if (model == "custom") {
        QString error;
        QStringList vars{"x"};
        vars << params;
        const Expression e = Expression::parse(formula, vars, &error);
        if (!e.isValid()) fail(error);
        else if (params.isEmpty()) fail(tr("The model needs at least one parameter besides x"));
        else {
            num::Vec p0 = evalList(text("p0"), &error);
            if (p0.size() != size_t(params.size())) p0.assign(size_t(params.size()), 1.0); // curve_fit's default
            const auto fit = num::curveFit(
                [&](double xv, const num::Vec &p) {
                    std::vector<double> values(size_t(params.size()) + 1);
                    values[0] = xv;
                    std::copy(p.begin(), p.end(), values.begin() + 1);
                    return e.eval(values);
                },
                x, y, p0);
            if (!fit.ok) fail(fit.error);
            else {
                heading(tr("Curve fit (optimize.curve_fit)"));
                QStringList sig;
                for (int i = 0; i < params.size(); ++i) {
                    value(params[i], QString("%1 ± %2").arg(fmt(fit.params[size_t(i)], 7), fmt(fit.stderrs[size_t(i)], 3)),
                          pyNumber(fit.params[size_t(i)]));
                    sig << params[i];
                }
                const num::Vec best = fit.params;
                curve = [e, best, params](double v) {
                    std::vector<double> values(size_t(params.size()) + 1);
                    values[0] = v;
                    std::copy(best.begin(), best.end(), values.begin() + 1);
                    return e.eval(values);
                };
                value("R²", rSquared(curve));
                code << QString("def modell(x, %1):\n    return %2\n").arg(sig.join(", "), e.toPython());
                QString p0Text;
                if (!text("p0").trimmed().isEmpty()) p0Text = ", p0=" + pyList(p0, 20);
                code << QString("popt, pcov = optimize.curve_fit(modell, x, y%1)").arg(p0Text)
                     << "perr = np.sqrt(np.diag(pcov))  # Standardabweichung der Parameter" << "print(popt, perr)";
                curveCode = "y_fit = modell(x_fit, *popt)";
            }
        }
    } else {
        // Interpolation passes through every point.
        std::vector<size_t> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return x[a] < x[b]; });
        num::Vec xs, ys;
        for (size_t i : order) {
            xs.push_back(x[i]);
            ys.push_back(y[i]);
        }
        if (model == "spline") {
            auto spline = std::make_shared<num::CubicSpline>();
            QString error;
            if (!spline->fit(xs, ys, &error)) fail(error);
            else {
                heading(tr("Cubic spline (interpolate.CubicSpline)"));
                curve = [spline](double v) { return (*spline)(v); };
                code << "spline = interpolate.CubicSpline(x, y)";
                curveCode = "y_fit = spline(x_fit)";
            }
        } else {
            heading(tr("Linear interpolation (np.interp)"));
            curve = [xs, ys](double v) { return num::interp(v, xs, ys); };
            curveCode = "y_fit = np.interp(x_fit, x, y)";
        }
        if (at.empty()) note(tr("Enter x values under “Evaluate at” to read values between the points."));
    }

    PlotAxes axes;
    axes.xLabel = m_source->xLabel();
    axes.yLabel = m_source->yLabel();
    PlotSeries data;
    data.style = PlotSeries::Markers;
    data.label = tr("Data");
    data.color = QColor("#5ac8fa");
    data.markerSize = n > 300 ? 3 : 7;
    data.x = x;
    data.y = y;
    axes.series.push_back(data);
    if (curve) {
        const double lo = num::minimum(x), hi = num::maximum(x);
        PlotSeries fitted;
        fitted.label = model == "spline" || model == "interp" ? tr("Interpolation") : tr("Fit");
        fitted.width = 2;
        for (int i = 0; i <= 600; ++i) {
            const double v = lo + (hi - lo) * i / 600;
            fitted.x.push_back(v);
            fitted.y.push_back(curve(v));
        }
        axes.series.push_back(fitted);
        if (!at.empty()) {
            heading(tr("Values"));
            PlotSeries points;
            points.style = PlotSeries::Markers;
            points.marker = 's';
            points.markerSize = 8;
            points.color = QColor("#ff6b6b");
            points.label = tr("Evaluated");
            const double span = std::max(1e-12, num::maximum(x) - num::minimum(x));
            for (double v : at) {
                const double r = curve(v);
                value(QString("y(%1)").arg(fmt(v)), r);
                // Slope of the model; for linear interpolation this is the
                // slope of the segment the point lies in.
                const double h = span * 1e-7;
                value(QString("dy/dx(%1)").arg(fmt(v)), (curve(v + h) - curve(v - h)) / (2 * h));
                points.x.push_back(v);
                points.y.push_back(r);
            }
            axes.series.push_back(points);
            code << QString("x_neu = np.array(%1)").arg(pyList(at, 100)) << QString(curveCode).replace("x_fit", "x_neu").replace("y_fit", "y_neu") << "print(y_neu)";
        }
        code << "" << "x_fit = np.linspace(x.min(), x.max(), 500)" << curveCode << "plt.figure(figsize=(10, 5))"
             << "plt.plot(x, y, 'ob', label='Messdaten')" << "plt.plot(x_fit, y_fit, 'r', label='Modell')" << "plt.legend()"
             << "plt.grid()" << "plt.show()";
    }
    m_figure.set({axes});
    publish(code.join('\n') + '\n');
}
