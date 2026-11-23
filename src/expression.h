#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

// A small math expression language that reads like the NumPy the course
// uses: `2.5*np.sin(2*pi*25*t) + 1.5*randn()`, `4*pi**2*L/T^2`. Expressions
// are parsed once into a tree, evaluated many times (one call per sample),
// differentiated symbolically for error propagation, and printed back as
// Python so every result can be reproduced in a notebook.
class Expression {
public:
    struct Node;
    using NodePtr = std::shared_ptr<const Node>;

    Expression() = default;

    // Parses `text`, where `variables` are the names the caller will pass
    // values for, in that order. Unknown names are an error.
    static Expression parse(const QString &text, const QStringList &variables, QString *error = nullptr);

    // The names in `text` that are neither functions nor constants, in the
    // order they first appear; used to discover a formula's variables.
    static QStringList freeNames(const QString &text);

    bool isValid() const { return bool(m_root); }
    double eval(const double *values) const;
    double eval(const std::vector<double> &values) const { return eval(values.data()); }
    double eval() const { return eval(nullptr); }

    // d/d(variable index); invalid when a function has no derivative (floor).
    Expression derivative(int variable) const;
    bool dependsOn(int variable) const;

    // Python/NumPy source for the expression; `sampleCount` sizes random
    // noise, e.g. "len(t)" turns randn() into np.random.randn(len(t)).
    QString toPython(const QString &sampleCount = {}) const;
    QStringList variables() const { return m_variables; }

    static QStringList functionNames();
    static void seedRandom(unsigned seed);

private:
    NodePtr m_root;
    QStringList m_variables;
};

// Evaluates a plain number field that may hold an expression ("2*pi*50",
// "1/5120", "1e-3"). Empty text gives `fallback` and no error.
double evalNumber(const QString &text, bool *ok = nullptr, double fallback = 0, QString *error = nullptr);

// Parses a list of numbers separated by commas, semicolons, whitespace, or
// newlines; each item may be an expression.
std::vector<double> evalList(const QString &text, QString *error = nullptr);

// Formats a value with `digits` significant digits in the current locale,
// switching to scientific notation for very large or small magnitudes.
QString formatNumber(double value, int digits = 6);

// Python literal for a number, always with a '.', for generated code.
QString pyNumber(double value);
