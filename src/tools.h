#pragma once

#include "datasource.h"
#include "expression.h"
#include "figure.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// One page of the app: text inputs in, results + a figure + the equivalent
// Python out. Inputs live in one map so QML can bind any field with
// `tool.inputs.key` and write it with `tool.set("key", text)`; the same map
// is what the session file stores.
class Tool : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap inputs READ inputs NOTIFY inputsChanged)
    Q_PROPERTY(Figure *figure READ figure CONSTANT)
    Q_PROPERTY(DataSource *source READ source CONSTANT)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QString code READ code NOTIFY resultsChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultsChanged)

public:
    explicit Tool(QObject *parent = nullptr);

    QVariantMap inputs() const { return m_inputs; }
    Figure *figure() { return &m_figure; }
    DataSource *source() const { return m_source; }
    QVariantList results() const { return m_results; }
    QString code() const { return m_code; }
    QString error() const { return m_error; }

    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    Q_INVOKABLE void recompute();

    virtual QVariantMap state() const;
    virtual void setState(const QVariantMap &state);

signals:
    void inputsChanged();
    void resultsChanged();
    void stateChanged();

protected:
    virtual void compute() = 0;
    void schedule() { m_timer.start(); }
    void setDefaults(const QVariantMap &defaults);
    void setSource(DataSource *source);

    QString text(const QString &key) const { return m_inputs.value(key).toString(); }
    bool flag(const QString &key) const { return m_inputs.value(key).toBool(); }
    // Evaluates an input as a number; an invalid one records an error
    // naming `label` and returns NaN.
    double number(const QString &key, const QString &label, double fallback = NAN);

    // Builders for `results`.
    void heading(const QString &title);
    void value(const QString &label, const QString &value, const QString &python = {});
    void value(const QString &label, double v, const QString &unit = {}, const QString &python = {});
    void answer(const QString &label, const QString &value);
    void note(const QString &text);
    void fail(const QString &message);
    void publish(const QString &code);

    QVariantMap m_inputs;
    Figure m_figure;
    DataSource *m_source = nullptr;
    QVariantList m_results, m_pending;
    QString m_code, m_error, m_pendingError;
    QTimer m_timer;
};

class CalculatorTool : public Tool {
    Q_OBJECT
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QVariantList variables READ variables NOTIFY historyChanged)

public:
    explicit CalculatorTool(QObject *parent = nullptr);
    QVariantList history() const { return m_history; }
    QVariantList variables() const;
    Q_INVOKABLE QString evaluate(const QString &line); // returns the formatted result or error
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString previous(int back) const;      // input history for Up/Down

    QVariantMap state() const override;
    void setState(const QVariantMap &state) override;

signals:
    void historyChanged();

protected:
    void compute() override;

private:
    QVariantList m_history;
    QStringList m_names;
    std::vector<double> m_values;
};

class FunctionTool : public Tool {
    Q_OBJECT
public:
    explicit FunctionTool(QObject *parent = nullptr);
protected:
    void compute() override;
};

class VectorTool : public Tool {
    Q_OBJECT
public:
    explicit VectorTool(QObject *parent = nullptr);
protected:
    void compute() override;
};

class StatsTool : public Tool {
    Q_OBJECT
public:
    explicit StatsTool(QObject *parent = nullptr);
protected:
    void compute() override;
private:
    void computeSummary();
};

class PropagationTool : public Tool {
    Q_OBJECT
    Q_PROPERTY(QStringList variables READ variables NOTIFY variablesChanged)
public:
    explicit PropagationTool(QObject *parent = nullptr);
    QStringList variables() const { return m_variables; }
signals:
    void variablesChanged();
protected:
    void compute() override;
private:
    QStringList m_variables;
};

class SignalTool : public Tool {
    Q_OBJECT
    Q_PROPERTY(QVariantList steps READ steps NOTIFY stepsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stepsChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stepsChanged)
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY resultsChanged)
public:
    explicit SignalTool(QObject *parent = nullptr);
    QVariantList steps() const { return m_inputs.value("steps").toList(); }
    QVariantList peaks() const { return m_peaks; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    Q_INVOKABLE void addStep(const QString &type);
    Q_INVOKABLE void removeStep(int index);
    Q_INVOKABLE void moveStep(int index, int delta);
    Q_INVOKABLE void setStep(int index, const QString &key, const QVariant &value);
    Q_INVOKABLE void toggleStep(int index);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE static QVariantList stepTypes();
signals:
    void stepsChanged();
protected:
    void compute() override;
private:
    void changeSteps(const QVariantList &steps, bool record = true);
    QList<QVariantList> m_undo, m_redo;
    QVariantList m_peaks;
};

class SpectrumTool : public Tool {
    Q_OBJECT
    Q_PROPERTY(QStringList windows READ windows CONSTANT)
public:
    explicit SpectrumTool(QObject *parent = nullptr);
    QStringList windows() const { return num::windowNames(); }
protected:
    void compute() override;
};

class FitTool : public Tool {
    Q_OBJECT
    Q_PROPERTY(QStringList parameters READ parameters NOTIFY parametersChanged)
public:
    explicit FitTool(QObject *parent = nullptr);
    QStringList parameters() const { return m_parameters; }
signals:
    void parametersChanged();
protected:
    void compute() override;
private:
    QStringList m_parameters;
};

// Column arithmetic on a whole table, like NumPy arrays: name the columns,
// define new ones row by row (x = (0.6 + delta_r)*cos(phi)), and plot.
class TableTool : public Tool {
    Q_OBJECT
    Q_PROPERTY(QStringList columns READ columns NOTIFY columnsChanged)
public:
    explicit TableTool(QObject *parent = nullptr);
    QStringList columns() const { return m_columns; }
signals:
    void columnsChanged();
protected:
    void compute() override;
private:
    QStringList m_columns;
};

// Shared helpers for tools.
QString pyList(const num::Vec &v, int max = 12);
QString fmt(double v, int digits = 6);
QString withUnit(const QString &value, const QString &unit);
