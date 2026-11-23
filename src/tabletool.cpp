#include "tools.h"

#include <QRegularExpression>
#include <cmath>

namespace {

// "phi_1 [rad]" -> "phi_1", "Zeit t [s]" -> "t", "Column 2" -> "c2".
QString identifierFor(const QString &header, int index) {
    QString h = header;
    h.remove(QRegularExpression(R"(\[[^\]]*\]|\([^)]*\))"));
    static const QRegularExpression word(R"([A-Za-z_][A-Za-z0-9_]*)");
    QString last;
    auto it = word.globalMatch(h);
    while (it.hasNext()) last = it.next().captured();
    if (last.isEmpty() || last == "Column" || last == "Spalte" || last == "Audio") return QString("c%1").arg(index);
    return last;
}

} // namespace

TableTool::TableTool(QObject *parent) : Tool(parent) {
    setSource(new DataSource(false, this));
    // A robot arm of 0.6 m, turned by phi and extended by delta_r.
    m_source->setPasteText("0\t0\t0\n0.5\t0.3\t0.05\n1\t0.6\t0.1\n1.5\t0.9\t0.15\n2\t1.2\t0.2\n2.5\t1.5\t0.2\n3\t1.8\t0.2");
    setDefaults({{"names", "t, phi, delta_r"},
                 {"formulas", "L = 0.6 + delta_r\nx = L*cos(phi)\ny = L*sin(phi)"},
                 {"x", "x"}, {"y", "y"}, {"equal", true}, {"markers", false}, {"arm", true}, {"row", "0"}});
}

void TableTool::compute() {
    const DataTable &table = m_source->table();
    if (!m_source->error().isEmpty() || table.columns.empty()) {
        fail(m_source->error().isEmpty() ? tr("No data") : m_source->error());
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    const int rows = table.rows();
    QStringList names;
    QStringList given = text("names").split(',', Qt::SkipEmptyParts);
    for (QString &g : given) g = g.trimmed();
    static const QRegularExpression valid(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    for (int c = 0; c < int(table.columns.size()); ++c) {
        QString n = c < given.size() && valid.match(given[c]).hasMatch() ? given[c] : identifierFor(table.names.value(c), c);
        if (names.contains(n)) n = QString("c%1").arg(c);
        names << n;
    }
    std::vector<num::Vec> columns = table.columns;

    QStringList code{"import numpy as np", "import matplotlib.pyplot as plt", ""};
    code << m_source->tablePython().trimmed();
    QStringList unpack;
    for (int c = 0; c < names.size(); ++c) unpack << (columns.size() == 1 ? "data" : QString("data[:, %1]").arg(c));
    code << names.join(", ") + " = " + unpack.join(", ") << "N = len(" + names.first() + ")" << "i = np.arange(N)  # Zeilenindex" << "";

    // Formulas, evaluated row by row in order; each may use the columns and
    // the ones defined above it, plus the row index i and the length N.
    int lineNo = 0;
    for (const QString &raw : text("formulas").split('\n')) {
        ++lineNo;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        static const QRegularExpression assign(R"(^([A-Za-z_]\w*)\s*=(?!=)\s*(.+)$)");
        const auto m = assign.match(line);
        if (!m.hasMatch()) {
            fail(tr("Line %1: write name = formula").arg(lineNo));
            continue;
        }
        const QString name = m.captured(1);
        QStringList vars = names;
        vars << "i" << "N";
        QString error;
        const Expression e = Expression::parse(m.captured(2), vars, &error);
        if (!e.isValid()) {
            fail(tr("Line %1: %2").arg(lineNo).arg(error));
            continue;
        }
        num::Vec out(static_cast<size_t>(rows));
        std::vector<double> values(size_t(vars.size()));
        values[size_t(vars.size() - 1)] = rows;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < names.size(); ++c) values[size_t(c)] = columns[size_t(c)][size_t(r)];
            values[size_t(names.size())] = r;
            out[size_t(r)] = e.eval(values);
        }
        const int existing = int(names.indexOf(name));
        if (existing >= 0) columns[size_t(existing)] = out;
        else {
            names << name;
            columns.push_back(out);
        }
        // Scalars (no column used) broadcast in NumPy as well.
        code << QString("%1 = %2").arg(name, e.toPython("N"));
    }
    if (names != m_columns) {
        m_columns = names;
        emit columnsChanged();
    }

    heading(tr("%n rows", "", rows));
    for (int c = 0; c < names.size(); ++c) {
        const num::Vec &v = columns[size_t(c)];
        value(names[c], QString("%1 … %2   (min %3, max %4, mean %5)")
                            .arg(fmt(v.front()), fmt(v.back()), fmt(num::minimum(v)), fmt(num::maximum(v)), fmt(num::mean(v))));
    }
    const int row = int(number("row", tr("Row")));
    if (row >= 0 && row < rows) {
        heading(tr("Row %1").arg(row));
        for (int c = 0; c < names.size(); ++c) value(QString("%1[%2]").arg(names[c]).arg(row), columns[size_t(c)][size_t(row)]);
    } else if (!std::isnan(double(row))) {
        note(tr("Row must be between 0 and %1").arg(rows - 1));
    }

    // Plot y column(s) against x.
    const QString xName = text("x").trimmed();
    const int xi = int(names.indexOf(xName));
    num::Vec xs = xi >= 0 ? columns[size_t(xi)] : num::Vec();
    if (xi < 0) {
        xs.resize(size_t(rows));
        for (int r = 0; r < rows; ++r) xs[size_t(r)] = r;
    }
    PlotAxes axes;
    axes.xLabel = xi >= 0 ? xName : tr("Index");
    axes.equal = flag("equal");
    QStringList plotted;
    for (QString yName : text("y").split(',', Qt::SkipEmptyParts)) {
        yName = yName.trimmed();
        const int yi = int(names.indexOf(yName));
        if (yi < 0) {
            fail(tr("Unknown column \"%1\"; available: %2").arg(yName, names.join(", ")));
            continue;
        }
        PlotSeries s;
        s.label = yName;
        s.x = xs;
        s.y = columns[size_t(yi)];
        s.style = flag("markers") ? PlotSeries::LineMarkers : PlotSeries::Line;
        s.markerSize = 4;
        s.width = 2;
        axes.series.push_back(s);
        plotted << yName;
    }
    axes.yLabel = plotted.join(", ");
    const QString xPy = xi >= 0 ? xName : "i";
    code << "" << "plt.figure(figsize=(8, 6))";
    const QStringList colors{"r", "b", "g", "m", "c"};
    for (int k = 0; k < plotted.size(); ++k)
        code << QString("plt.plot(%1, %2, '%3', label='%2')").arg(xPy, plotted[k], colors[k % colors.size()]);
    if (flag("arm") && row >= 0 && row < rows && !plotted.isEmpty()) {
        // The arm (or any position vector) from the origin to the point in
        // the chosen row, as the exam's "Startstellung".
        const double px = xs[size_t(row)], py = axes.series.front().y[size_t(row)];
        PlotSeries arm;
        arm.style = PlotSeries::LineMarkers;
        arm.label = tr("Vector to row %1").arg(row);
        arm.color = QColor("#e6e6e6");
        arm.width = 3;
        arm.markerSize = 7;
        arm.x = {0, px};
        arm.y = {0, py};
        axes.series.push_back(arm);
        code << QString("plt.plot([0, %1[%3]], [0, %2[%3]], 'k-o', lw=3, label='Arm (Zeile %3)')").arg(xPy, plotted.first()).arg(row);
    }
    if (axes.equal) code << "plt.axis('equal')";
    code << QString("plt.xlabel('%1')").arg(axes.xLabel) << QString("plt.ylabel('%1')").arg(axes.yLabel) << "plt.grid()"
         << "plt.legend()" << "plt.savefig('diagramm.svg')  # vor plt.show() speichern" << "plt.show()";
    m_figure.set({axes});
    publish(code.join('\n') + '\n');
}
