#include "tools.h"

#include <QRegularExpression>
#include <cmath>

QString fmt(double v, int digits) { return formatNumber(v, digits); }

QString withUnit(const QString &value, const QString &unit) {
    return unit.trimmed().isEmpty() ? value : value + " " + unit.trimmed();
}

QString pyList(const num::Vec &v, int max) {
    QStringList items;
    for (size_t i = 0; i < v.size() && int(i) < max; ++i) items << pyNumber(v[i]);
    if (int(v.size()) > max) items << "...";
    return "[" + items.join(", ") + "]";
}

Tool::Tool(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &Tool::recompute);
}

void Tool::setDefaults(const QVariantMap &defaults) {
    m_inputs = defaults;
    schedule();
}

void Tool::setSource(DataSource *source) {
    m_source = source;
    connect(source, &DataSource::changed, this, &Tool::schedule);
    connect(source, &DataSource::inputChanged, this, &Tool::stateChanged);
}

void Tool::set(const QString &key, const QVariant &value) {
    if (m_inputs.value(key) == value) return;
    m_inputs[key] = value;
    emit inputsChanged();
    emit stateChanged();
    schedule();
}

void Tool::recompute() {
    m_timer.stop();
    m_pending.clear();
    m_pendingError.clear();
    compute();
    m_results = m_pending;
    m_error = m_pendingError;
    emit resultsChanged();
}

QVariantMap Tool::state() const {
    QVariantMap s{{"inputs", m_inputs}};
    if (m_source) s["source"] = m_source->state();
    return s;
}

void Tool::setState(const QVariantMap &state) {
    const QVariantMap saved = state.value("inputs").toMap();
    for (auto it = saved.begin(); it != saved.end(); ++it) m_inputs[it.key()] = it.value();
    if (m_source) m_source->setState(state.value("source").toMap());
    emit inputsChanged();
    schedule();
}

double Tool::number(const QString &key, const QString &label, double fallback) {
    bool ok = true;
    QString message;
    const double v = evalNumber(text(key), &ok, fallback, &message);
    if (!ok) {
        fail(QString("%1: %2").arg(label, message));
        return NAN;
    }
    return v;
}

void Tool::heading(const QString &title) { m_pending << QVariantMap{{"kind", "heading"}, {"label", title}}; }

void Tool::value(const QString &label, const QString &v, const QString &python) {
    m_pending << QVariantMap{{"kind", "value"}, {"label", label}, {"value", v}, {"copy", python.isEmpty() ? v : python}};
}

void Tool::value(const QString &label, double v, const QString &unit, const QString &python) {
    Q_UNUSED(python)
    m_pending << QVariantMap{{"kind", "value"}, {"label", label}, {"value", withUnit(fmt(v), unit)}, {"copy", pyNumber(v)}};
}

void Tool::answer(const QString &label, const QString &v) {
    m_pending << QVariantMap{{"kind", "answer"}, {"label", label}, {"value", v}, {"copy", v}};
}

void Tool::note(const QString &text) { m_pending << QVariantMap{{"kind", "note"}, {"label", text}}; }

void Tool::fail(const QString &message) {
    if (m_pendingError.isEmpty()) m_pendingError = message;
}

void Tool::publish(const QString &code) { m_code = code; }

// --- Calculator -----------------------------------------------------------

CalculatorTool::CalculatorTool(QObject *parent) : Tool(parent) {}

void CalculatorTool::compute() {}

QVariantList CalculatorTool::variables() const {
    QVariantList list;
    for (int i = 0; i < m_names.size(); ++i)
        list << QVariantMap{{"name", m_names[i]}, {"value", fmt(m_values[size_t(i)], 10)}, {"copy", pyNumber(m_values[size_t(i)])}};
    return list;
}

QString CalculatorTool::evaluate(const QString &input) {
    const QString line = input.trimmed();
    if (line.isEmpty()) return {};
    static const QRegularExpression assignment(R"(^([A-Za-z_]\w*)\s*=(?!=)\s*(.+)$)");
    const auto m = assignment.match(line);
    const QString name = m.hasMatch() ? m.captured(1) : QString();
    const QString formula = m.hasMatch() ? m.captured(2) : line;
    QString error;
    const Expression e = Expression::parse(formula, m_names, &error);
    QVariantMap entry{{"input", line}};
    QString shown;
    if (!e.isValid()) {
        entry["error"] = error;
        shown = error;
    } else {
        const double v = e.eval(m_values);
        auto store = [&](const QString &n) {
            const int i = int(m_names.indexOf(n));
            if (i >= 0) m_values[size_t(i)] = v;
            else {
                m_names << n;
                m_values.push_back(v);
            }
        };
        if (!name.isEmpty()) store(name);
        store("ans");
        shown = fmt(v, 12);
        entry["output"] = shown;
        entry["copy"] = pyNumber(v);
        entry["python"] = name.isEmpty() ? "print(" + e.toPython() + ")" : name + " = " + e.toPython();
    }
    m_history << entry;
    QStringList code{"import numpy as np", "from scipy import constants", ""};
    for (const auto &h : m_history) {
        const auto map = h.toMap();
        if (map.contains("python")) code << map.value("python").toString();
    }
    publish(code.join('\n') + '\n');
    emit resultsChanged();
    emit historyChanged();
    emit stateChanged();
    return shown;
}

QString CalculatorTool::previous(int back) const {
    int seen = 0;
    for (int i = int(m_history.size()) - 1; i >= 0; --i)
        if (++seen == back) return m_history[i].toMap().value("input").toString();
    return {};
}

void CalculatorTool::clear() {
    m_history.clear();
    m_names.clear();
    m_values.clear();
    publish({});
    emit resultsChanged();
    emit historyChanged();
    emit stateChanged();
}

QVariantMap CalculatorTool::state() const {
    QVariantMap s = Tool::state();
    QStringList inputs;
    for (const auto &h : m_history) inputs << h.toMap().value("input").toString();
    s["lines"] = inputs;
    return s;
}

void CalculatorTool::setState(const QVariantMap &state) {
    Tool::setState(state);
    // Replaying the lines restores the variables they defined.
    for (const QString &line : state.value("lines").toStringList()) evaluate(line);
}
