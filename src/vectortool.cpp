#include "tools.h"

#include <QRegularExpression>
#include <cmath>

namespace {

using Vec = num::Vec;
using Matrix = num::Matrix;

// "2.1, 3.5", "[1, 2, 3]", or polar "5@30" (magnitude 5 at 30°).
Vec parseVector(const QString &input, QString *error) {
    QString text = input.trimmed();
    if (text.isEmpty()) return {};
    static const QRegularExpression polar(R"(^\s*(.+?)\s*(?:@|∠|<)\s*(.+?)\s*°?\s*$)");
    const auto m = polar.match(text);
    if (m.hasMatch()) {
        bool ok1 = false, ok2 = false;
        const double r = evalNumber(m.captured(1), &ok1), phi = evalNumber(m.captured(2), &ok2) * M_PI / 180;
        if (!ok1 || !ok2) {
            *error = QObject::tr("Write polar vectors as magnitude@angle, e.g. 5@30");
            return {};
        }
        return {r * std::cos(phi), r * std::sin(phi)};
    }
    Vec v = evalList(text, error);
    if (!v.empty() && v.size() != 2 && v.size() != 3) {
        *error = QObject::tr("A vector has 2 or 3 components");
        return {};
    }
    return v;
}

Matrix parseMatrix(const QString &input, QString *error) {
    Matrix m;
    QString text = input;
    text.remove('[').remove(']');
    for (const QString &row : text.split(QRegularExpression("[;\n]"), Qt::SkipEmptyParts)) {
        if (row.trimmed().isEmpty()) continue;
        Vec r = evalList(row, error);
        if (r.empty()) return {};
        m.push_back(r);
    }
    for (const auto &r : m)
        if (r.size() != m[0].size()) {
            *error = QObject::tr("All rows of the matrix need the same number of entries (rows separated by ;)");
            return {};
        }
    return m;
}

QString show(const Vec &v) {
    QStringList items;
    for (double x : v) items << fmt(x);
    return "[" + items.join(",  ") + "]";
}

QString showMatrix(const Matrix &m) {
    QStringList rows;
    for (const auto &r : m) rows << show(r);
    return rows.join("\n");
}

QString pyVec(const Vec &v) {
    QStringList items;
    for (double x : v) items << pyNumber(x);
    return "np.array([" + items.join(", ") + "])";
}

double norm(const Vec &v) {
    double s = 0;
    for (double x : v) s += x * x;
    return std::sqrt(s);
}

double dot(const Vec &a, const Vec &b) {
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

Vec add(const Vec &a, const Vec &b, double f = 1) {
    Vec r(a.size());
    for (size_t i = 0; i < a.size(); ++i) r[i] = a[i] + f * b[i];
    return r;
}

Vec scaled(const Vec &a, double f) {
    Vec r = a;
    for (double &x : r) x *= f;
    return r;
}

Vec mul(const Matrix &m, const Vec &v) {
    Vec r(m.size(), 0.0);
    for (size_t i = 0; i < m.size(); ++i)
        for (size_t j = 0; j < v.size(); ++j) r[i] += m[i][j] * v[j];
    return r;
}

Matrix transpose(const Matrix &m) {
    Matrix t(m[0].size(), Vec(m.size()));
    for (size_t i = 0; i < m.size(); ++i)
        for (size_t j = 0; j < m[i].size(); ++j) t[j][i] = m[i][j];
    return t;
}

Matrix rotation(int dims, double phi, const QString &axis) {
    const double c = std::cos(phi), s = std::sin(phi);
    if (dims == 2) return {{c, -s}, {s, c}};
    if (axis == "x") return {{1, 0, 0}, {0, c, -s}, {0, s, c}};
    if (axis == "y") return {{c, 0, s}, {0, 1, 0}, {-s, 0, c}};
    return {{c, -s, 0}, {s, c, 0}, {0, 0, 1}};
}

// Rounding noise like 6.1e-17 becomes 0 so results read cleanly.
Vec clean(Vec v) {
    double scale = 0;
    for (double x : v) scale = std::max(scale, std::abs(x));
    for (double &x : v)
        if (std::abs(x) < 1e-12 * std::max(1.0, scale)) x = 0;
    return v;
}

} // namespace

VectorTool::VectorTool(QObject *parent) : Tool(parent) {
    setDefaults({{"a", "2.1, 3.5"}, {"b", "-1.5, 4.0"}, {"c", "0.5, -0.5"}, {"angle", "90"}, {"axis", "z"},
                 {"t", "10, 2"}, {"m", ""}});
}

void VectorTool::compute() {
    QString error;
    const Vec a = parseVector(text("a"), &error);
    if (!error.isEmpty()) fail("a: " + error);
    error.clear();
    const Vec b = parseVector(text("b"), &error);
    if (!error.isEmpty()) fail("b: " + error);
    error.clear();
    const Vec c = parseVector(text("c"), &error);
    if (!error.isEmpty()) fail("c: " + error);
    error.clear();
    const Vec t = parseVector(text("t"), &error);
    if (!error.isEmpty()) fail("t: " + error);
    error.clear();
    const Matrix M = parseMatrix(text("m"), &error);
    if (!error.isEmpty()) fail("M: " + error);
    const double angle = number("angle", tr("Angle"));
    const QString axis = text("axis").isEmpty() ? "z" : text("axis");

    if (a.empty()) {
        if (m_pendingError.isEmpty()) fail(tr("Enter vector a, e.g. 2.1, 3.5 or 5@30 (polar)"));
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    const int dims = int(a.size());
    const bool hasB = b.size() == a.size(), hasC = c.size() == a.size(), hasT = t.size() == a.size();
    if (!b.empty() && !hasB) fail(tr("a and b need the same number of components"));
    if (!c.empty() && !hasC) fail(tr("a and c need the same number of components"));
    if (!t.empty() && !hasT) fail(tr("a and t need the same number of components"));

    QStringList code{"import numpy as np", "import matplotlib.pyplot as plt", ""};
    code << "a = " + pyVec(a);
    if (hasB) code << "b = " + pyVec(b);
    if (hasC) code << "c = " + pyVec(c);
    if (hasT) code << "t = " + pyVec(t) + "  # Verschiebung";

    auto describe = [&](const QString &name, const Vec &v) {
        heading(tr("Vector %1 = %2").arg(name, show(v)));
        value(QString("|%1|").arg(name), norm(v), {});
        code << QString("print(np.linalg.norm(%1))  # Betrag").arg(name);
        if (dims == 2) {
            const double phi = std::atan2(v[1], v[0]) * 180 / M_PI;
            value(tr("Angle φ (arctan2)"), fmt(phi) + "°", pyNumber(phi));
            code << QString("print(np.degrees(np.arctan2(%1[1], %1[0])))  # Winkel [°]").arg(name);
        } else {
            const double n = norm(v);
            value(tr("Angles to x, y, z"), QString("%1°,  %2°,  %3°")
                                                .arg(fmt(std::acos(v[0] / n) * 180 / M_PI), fmt(std::acos(v[1] / n) * 180 / M_PI),
                                                     fmt(std::acos(v[2] / n) * 180 / M_PI)));
            value(tr("Azimuth φ in xy (arctan2)"), fmt(std::atan2(v[1], v[0]) * 180 / M_PI) + "°");
        }
        if (norm(v) > 0) {
            value(tr("Unit vector"), show(scaled(v, 1 / norm(v))));
            code << QString("print(%1 / np.linalg.norm(%1))  # Einheitsvektor").arg(name);
        }
    };
    describe("a", a);
    if (hasB) describe("b", b);

    if (hasB) {
        heading(tr("a and b"));
        value("a + b", show(add(a, b)));
        value("a − b", show(add(a, b, -1)));
        value(tr("a · b (dot product)"), dot(a, b));
        code << "print(a + b, a - b)" << "print(np.dot(a, b))  # Skalarprodukt";
        if (norm(a) > 0 && norm(b) > 0) {
            const double cosine = std::clamp(dot(a, b) / (norm(a) * norm(b)), -1.0, 1.0);
            value(tr("Angle between a and b"), fmt(std::acos(cosine) * 180 / M_PI) + "°", pyNumber(std::acos(cosine) * 180 / M_PI));
            code << "print(np.degrees(np.arccos(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)))))  # Zwischenwinkel";
            value(tr("Projection of a on b"), dot(a, b) / norm(b));
            value(tr("Component of a along b"), show(scaled(b, dot(a, b) / dot(b, b))));
        }
        if (dims == 3) {
            const Vec cross{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
            value(tr("a × b (cross product)"), show(cross));
            value(tr("|a × b| (parallelogram area)"), norm(cross));
            code << "print(np.cross(a, b))  # Kreuzprodukt";
        } else {
            const double z = a[0] * b[1] - a[1] * b[0];
            value(tr("a × b (z component)"), z);
            value(tr("|a × b| (parallelogram area)"), std::abs(z));
            code << "print(a[0]*b[1] - a[1]*b[0])  # Kreuzprodukt (z-Komponente, 2D)";
        }
    }
    Vec res = a;
    if (hasB) res = add(res, b);
    if (hasC) res = add(res, c);
    if (hasC) {
        heading(tr("Resultant a + b + c"));
        value(tr("Resultant"), show(res));
        value(tr("|resultant|"), norm(res));
        if (dims == 2) value(tr("Angle φ (arctan2)"), fmt(std::atan2(res[1], res[0]) * 180 / M_PI) + "°");
        code << "res = a + b + c" << "print(res, np.linalg.norm(res), np.degrees(np.arctan2(res[1], res[0])))";
    }

    Vec rotated;
    if (!std::isnan(angle)) {
        const Matrix R = rotation(dims, angle * M_PI / 180, axis);
        heading(dims == 2 ? tr("Rotation by φ = %1°").arg(fmt(angle)) : tr("Rotation by φ = %1° about %2").arg(fmt(angle), axis));
        value(dims == 2 ? "R(φ)" : QString("R%1(φ)").arg(axis), showMatrix(R));
        rotated = clean(mul(R, a));
        value(tr("R·a  (rotate a, active)"), show(rotated));
        value(tr("Rᵀ·a  (a in the rotated system, passive)"), show(clean(mul(transpose(R), a))));
        code << "" << QString("phi = np.deg2rad(%1)").arg(pyNumber(angle));
        if (dims == 2)
            code << "R = np.array([[np.cos(phi), -np.sin(phi)],\n              [np.sin(phi),  np.cos(phi)]])";
        else if (axis == "x")
            code << "R = np.array([[1, 0, 0],\n              [0, np.cos(phi), -np.sin(phi)],\n              [0, np.sin(phi),  np.cos(phi)]])";
        else if (axis == "y")
            code << "R = np.array([[ np.cos(phi), 0, np.sin(phi)],\n              [0, 1, 0],\n              [-np.sin(phi), 0, np.cos(phi)]])";
        else
            code << "R = np.array([[np.cos(phi), -np.sin(phi), 0],\n              [np.sin(phi),  np.cos(phi), 0],\n              [0, 0, 1]])";
        code << "print(R @ a)  # aktive Drehung" << "print(R.T @ a)  # Koordinaten im gedrehten System";
        if (hasT) {
            value(tr("R·a + t  (rotate, then shift)"), show(clean(add(mul(R, a), t))));
            value(tr("R·(a + t)  (shift, then rotate)"), show(clean(mul(R, add(a, t)))));
            value(tr("a − t  (a in the shifted system)"), show(clean(add(a, t, -1))));
            value(tr("Rᵀ·(a − t)  (a in the rotated and shifted system)"), show(clean(mul(transpose(R), add(a, t, -1)))));
            code << "print(R @ a + t)  # drehen, dann verschieben" << "print(R @ (a + t))  # verschieben, dann drehen"
                 << "print(R.T @ (a - t))  # Koordinaten im gedrehten und verschobenen System";
        }
    }

    if (!M.empty()) {
        const int n = int(M.size());
        const bool square = M[0].size() == M.size();
        heading(tr("Matrix M (%1×%2)").arg(n).arg(M[0].size()));
        value("M", showMatrix(M));
        value("Mᵀ", showMatrix(transpose(M)));
        // A rotation matrix is square, orthogonal (MᵀM = I), and has det = +1.
        if (square) {
            const double det = num::determinant(M);
            value("det(M)", det);
            Matrix inv;
            if (num::inverse(M, &inv)) value("M⁻¹", showMatrix(inv));
            else value("M⁻¹", tr("singular, no inverse"));
            double off = 0;
            for (int r = 0; r < n; ++r)
                for (int c = 0; c < n; ++c) {
                    double s = 0;
                    for (int k = 0; k < n; ++k) s += M[size_t(k)][size_t(r)] * M[size_t(k)][size_t(c)];
                    off = std::max(off, std::abs(s - (r == c ? 1.0 : 0.0)));
                }
            const bool rotation = off < 1e-6 && std::abs(det - 1) < 1e-6;
            answer(tr("Rotation matrix?"), rotation ? tr("Yes: MᵀM = I and det = 1")
                                                    : off >= 1e-6 ? tr("No: MᵀM ≠ I (columns not unit length or not perpendicular)")
                                                                  : tr("No: det = %1, not +1 (a reflection)").arg(fmt(det)));
            if (n == 2 && rotation) value(tr("Angle φ = arctan2(M₂₁, M₁₁)"), fmt(std::atan2(M[1][0], M[0][0]) * 180 / M_PI) + "°");
        } else {
            answer(tr("Rotation matrix?"), tr("No: not square"));
        }
        if (int(M[0].size()) == dims) value("M·a", show(mul(M, a)));
        if (int(M[0].size()) == dims && hasB) value("M·b", show(mul(M, b)));
        QStringList rows;
        for (const auto &r : M) {
            QStringList items;
            for (double x : r) items << pyNumber(x);
            rows << "[" + items.join(", ") + "]";
        }
        code << "" << "M = np.array([" + rows.join(",\n              ") + "])" << (square ? "print(np.linalg.det(M))" : "print(M.shape)")
             << "print(M.T)" << "print(np.allclose(M.T @ M, np.eye(len(M))))  # Rotationsmatrix: M.T @ M = I und det = 1";
        if (square) code << "print(np.linalg.inv(M))";
        if (int(M[0].size()) == dims) code << "print(M @ a)";
    }

    // Vectors chained tip to tail, as in the course's vector addition plots.
    PlotAxes axes;
    axes.equal = true;
    axes.xLabel = "x";
    axes.yLabel = "y";
    if (dims == 3) axes.title = tr("Projection onto the xy plane");
    auto arrow = [&](const QString &label, const Vec &from, const Vec &v, const QColor &color, bool dashed = false) {
        PlotSeries s;
        s.style = PlotSeries::Arrows;
        s.label = label;
        s.color = color;
        s.dashed = dashed;
        s.width = 2;
        s.x = {from[0], from[0] + v[0]};
        s.y = {from[1], from[1] + v[1]};
        axes.series.push_back(s);
    };
    const Vec origin(size_t(dims), 0.0);
    arrow("a", origin, a, QColor("#ff6b6b"));
    if (hasB) arrow("b", a, b, QColor("#7bd88f"));
    if (hasC) arrow("c", add(a, b), c, QColor("#5ac8fa"));
    if (hasB) arrow(hasC ? tr("Resultant") : "a + b", origin, res, QColor("#e6e6e6"), true);
    if (!rotated.empty() && std::isfinite(angle) && angle != 0) arrow("R·a", origin, rotated, QColor("#c792ea"));
    m_figure.set({axes});

    code << "" << "plt.plot([0, a[0]], [0, a[1]], 'r', label='a')";
    if (hasB) code << "plt.plot([a[0], a[0] + b[0]], [a[1], a[1] + b[1]], 'g', label='b')";
    if (hasC) code << "plt.plot([a[0] + b[0], res[0]], [a[1] + b[1], res[1]], 'b', label='c')";
    if (hasC) code << "plt.plot([0, res[0]], [0, res[1]], 'k', label='res')";
    code << "plt.axis('equal')" << "plt.grid()" << "plt.legend()" << "plt.show()";
    publish(code.join('\n') + '\n');
}
