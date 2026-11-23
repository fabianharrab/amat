#include "tools.h"

#include <cmath>

QString roundedResult(double value, double U, int *decimals);

PropagationTool::PropagationTool(QObject *parent) : Tool(parent) {
    setDefaults({{"formula", "4*pi**2*L/T**2"}, {"name", "g"}, {"unit", "m/s²"}, {"k", "2"},
                 {"val:L", "1.546"}, {"u:L", "0.001"}, {"kind:L", "std"},
                 {"val:T", "2.465"}, {"u:T", "0.02171"}, {"kind:T", "std"}});
}

void PropagationTool::compute() {
    const QString formula = text("formula");
    const QStringList names = Expression::freeNames(formula);
    if (names != m_variables) {
        m_variables = names;
        emit variablesChanged();
    }
    const QString name = text("name").trimmed().isEmpty() ? "y" : text("name").trimmed();
    const QString unit = text("unit").trimmed();
    const double k = number("k", "k", 2);
    QString error;
    const Expression e = Expression::parse(formula, names, &error);
    if (!e.isValid()) {
        fail(error);
        m_figure.clear(error);
        publish({});
        return;
    }
    std::vector<double> x(size_t(names.size())), u(size_t(names.size()));
    for (int i = 0; i < names.size(); ++i) {
        const QString n = names[i];
        x[size_t(i)] = number("val:" + n, tr("Value of %1").arg(n));
        const double given = number("u:" + n, tr("Uncertainty of %1").arg(n), 0);
        // A tolerance ±a is a rectangular distribution: u = a/√3.
        u[size_t(i)] = text("kind:" + n) == "rect" ? given / std::sqrt(3.0) : given;
        if (text("val:" + n).trimmed().isEmpty()) fail(tr("Enter a value for %1").arg(n));
    }
    if (!m_pendingError.isEmpty()) {
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }

    const double y = e.eval(x);
    QStringList code{"import numpy as np", ""};
    for (int i = 0; i < names.size(); ++i) {
        const QString n = names[i];
        code << QString("%1 = %2").arg(n, pyNumber(x[size_t(i)]));
        if (text("kind:" + n) == "rect")
            code << QString("u_%1 = %2 / np.sqrt(3)  # Toleranz (Rechteckverteilung)").arg(n, text("u:" + n).trimmed());
        else code << QString("u_%1 = %2  # Standardunsicherheit").arg(n, pyNumber(u[size_t(i)]));
    }
    code << "" << QString("%1 = %2").arg(name, e.toPython()) << "";

    heading(tr("%1 = %2").arg(name, formula));
    value(name, y, unit);

    PlotAxes budget;
    budget.title = tr("Uncertainty budget");
    budget.yLabel = QString("|∂%1/∂xᵢ · uᵢ|").arg(name);
    PlotSeries bars;
    bars.style = PlotSeries::Bars;
    bars.barWidth = 0.6;

    double sumSquares = 0;
    QStringList terms;
    std::vector<double> contributions;
    for (int i = 0; i < names.size(); ++i) {
        const QString n = names[i];
        const Expression d = e.derivative(i);
        double slope;
        QString formulaText;
        if (d.isValid()) {
            slope = d.eval(x);
            formulaText = d.toPython();
        } else {
            // No symbolic rule (e.g. floor): central difference.
            std::vector<double> lo = x, hi = x;
            const double h = 1e-6 * std::max(1.0, std::abs(x[size_t(i)]));
            lo[size_t(i)] -= h;
            hi[size_t(i)] += h;
            slope = (e.eval(hi) - e.eval(lo)) / (2 * h);
            formulaText = tr("numeric");
        }
        const double c = slope * u[size_t(i)];
        contributions.push_back(c);
        sumSquares += c * c;
        heading(tr("Influence of %1").arg(n));
        value(QString("∂%1/∂%2").arg(name, n), formulaText, formulaText);
        value(QString("∂%1/∂%2 = ").arg(name, n), slope);
        value(QString("u(%1)").arg(n), u[size_t(i)]);
        value(QString("∂%1/∂%2 · u(%2)").arg(name, n), c, unit);
        code << QString("d%1_d%2 = %3  # Ableitung nach %2").arg(name, n, d.isValid() ? formulaText : pyNumber(slope));
        terms << QString("(d%1_d%2 * u_%2)**2").arg(name, n);
        bars.x.push_back(i + 1);
        bars.y.push_back(std::abs(c));
        budget.categories << n;
    }
    const double uy = std::sqrt(sumSquares), U = k * uy;
    heading(tr("Result"));
    for (int i = 0; i < names.size(); ++i)
        if (sumSquares > 0)
            value(tr("Share of %1").arg(names[i]), fmt(100 * contributions[size_t(i)] * contributions[size_t(i)] / sumSquares, 3) + " %");
    value(tr("Combined standard uncertainty u(%1)").arg(name), uy, unit);
    value(tr("Expanded uncertainty U = k·u"), U, unit);
    value(tr("Relative uncertainty U/|%1|").arg(name), fmt(100 * U / std::abs(y), 3) + " %");
    int decimals = 0;
    answer(tr("Result"), QString("%1 = %2  (k = %3)").arg(name, withUnit(roundedResult(y, U, &decimals), unit), fmt(k)));
    code << QString("u_%1 = np.sqrt(%2)  # Fehlerfortpflanzung").arg(name, terms.join(" + "))
         << QString("k = %1").arg(pyNumber(k)) << QString("U_%1 = k * u_%1").arg(name)
         << QString("print(f\"%1 = {%1:.%2f} +/- {U_%1:.%2f} %3  (k = {k})\")").arg(name).arg(std::max(0, decimals)).arg(unit);
    budget.series.push_back(bars);
    m_figure.set({budget});
    publish(code.join('\n') + '\n');
}
