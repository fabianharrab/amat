#include "tools.h"

#include <algorithm>
#include <cmath>

namespace {

QVariantMap param(const QString &key, const QString &label, const QString &value, const QStringList &options = {}) {
    return {{"key", key}, {"label", label}, {"default", value}, {"options", options}};
}

} // namespace

QVariantList SignalTool::stepTypes() {
    return {
        QVariantMap{{"type", "butter"}, {"label", tr("Butterworth filter")}, {"python", "signal.butter + sosfiltfilt"},
                    {"params", QVariantList{param("btype", tr("Type"), "lowpass", {"lowpass", "highpass", "bandpass", "bandstop"}),
                                            param("order", tr("Order"), "4"), param("f1", tr("f₁ [Hz]"), "10"),
                                            param("f2", tr("f₂ [Hz] (band)"), "1000")}}},
        QVariantMap{{"type", "savgol"}, {"label", tr("Savitzky–Golay smoothing")}, {"python", "signal.savgol_filter"},
                    {"params", QVariantList{param("window", tr("Window length"), "9"), param("polyorder", tr("Polynomial order"), "3")}}},
        QVariantMap{{"type", "medfilt"}, {"label", tr("Median filter")}, {"python", "signal.medfilt"},
                    {"params", QVariantList{param("kernel", tr("Kernel size (odd)"), "9")}}},
        QVariantMap{{"type", "detrend"}, {"label", tr("Remove trend")}, {"python", "signal.detrend"},
                    {"params", QVariantList{param("kind", tr("Type"), "linear", {"linear", "constant"})}}},
        QVariantMap{{"type", "decimate"}, {"label", tr("Downsample")}, {"python", "signal.decimate"},
                    {"params", QVariantList{param("q", tr("Factor q"), "10")}}},
        QVariantMap{{"type", "crop"}, {"label", tr("Time window")}, {"python", "(t > t1) & (t < t2)"},
                    {"params", QVariantList{param("t1", tr("from t₁ [s]"), "20"), param("t2", tr("to t₂ [s]"), "40")}}},
        QVariantMap{{"type", "mean"}, {"label", tr("Remove mean (offset)")}, {"python", "y - y.mean()"}, {"params", QVariantList{}}},
        QVariantMap{{"type", "scale"}, {"label", tr("Multiply by")}, {"python", "y * factor"},
                    {"params", QVariantList{param("factor", tr("Factor"), "1000")}}},
        QVariantMap{{"type", "gradient"}, {"label", tr("Derivative d/dt")}, {"python", "np.gradient"}, {"params", QVariantList{}}},
        QVariantMap{{"type", "integrate"}, {"label", tr("Integral ∫dt (cumulative)")}, {"python", "integrate.cumulative_trapezoid"},
                    {"params", QVariantList{}}},
        QVariantMap{{"type", "abs"}, {"label", tr("Absolute value")}, {"python", "np.abs"}, {"params", QVariantList{}}},
    };
}

SignalTool::SignalTool(QObject *parent) : Tool(parent) {
    setSource(new DataSource(true, this));
    m_source->setFormula("0.1*t + 0.5*sin(2*pi*0.1*t) + 0.3*sin(2*pi*3*t) + 0.1*randn()");
    m_source->setDurationText("20");
    m_source->setFsText("10");
    QVariantList steps{QVariantMap{{"type", "butter"}, {"enabled", true}, {"btype", "lowpass"}, {"order", "2"}, {"f1", "1"}, {"f2", ""}}};
    setDefaults({{"steps", steps}, {"original", true}, {"peaks", false}, {"height", ""}, {"distance", ""}, {"prominence", ""}});
}

void SignalTool::changeSteps(const QVariantList &steps, bool record) {
    if (record) {
        m_undo << this->steps();
        m_redo.clear();
    }
    m_inputs["steps"] = steps;
    emit inputsChanged();
    emit stepsChanged();
    emit stateChanged();
    schedule();
}

void SignalTool::addStep(const QString &type) {
    for (const auto &t : stepTypes()) {
        const auto map = t.toMap();
        if (map.value("type") != type) continue;
        QVariantMap step{{"type", type}, {"enabled", true}};
        for (const auto &p : map.value("params").toList()) step[p.toMap().value("key").toString()] = p.toMap().value("default");
        QVariantList s = steps();
        s << step;
        changeSteps(s);
        return;
    }
}

void SignalTool::removeStep(int index) {
    QVariantList s = steps();
    if (index < 0 || index >= s.size()) return;
    s.removeAt(index);
    changeSteps(s);
}

void SignalTool::moveStep(int index, int delta) {
    QVariantList s = steps();
    const int to = index + delta;
    if (index < 0 || index >= s.size() || to < 0 || to >= s.size()) return;
    s.move(index, to);
    changeSteps(s);
}

void SignalTool::setStep(int index, const QString &key, const QVariant &value) {
    QVariantList s = steps();
    if (index < 0 || index >= s.size()) return;
    QVariantMap step = s[index].toMap();
    if (step.value(key) == value) return;
    step[key] = value;
    s[index] = step;
    // Typing in a field is not an undo step of its own; structure changes are.
    changeSteps(s, false);
}

void SignalTool::toggleStep(int index) {
    QVariantList s = steps();
    if (index < 0 || index >= s.size()) return;
    QVariantMap step = s[index].toMap();
    step["enabled"] = !step.value("enabled", true).toBool();
    s[index] = step;
    changeSteps(s);
}

void SignalTool::undo() {
    if (m_undo.isEmpty()) return;
    m_redo << steps();
    changeSteps(m_undo.takeLast(), false);
}

void SignalTool::redo() {
    if (m_redo.isEmpty()) return;
    m_undo << steps();
    changeSteps(m_redo.takeLast(), false);
}

void SignalTool::compute() {
    m_peaks.clear();
    if (!m_source->error().isEmpty() || m_source->y().empty()) {
        fail(m_source->error().isEmpty() ? tr("No data") : m_source->error());
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    num::Vec t = m_source->x(), y = m_source->y();
    const num::Vec t0 = t, y0 = y;
    double fs = m_source->fs();
    if (!std::isfinite(fs) && t.size() > 1) fs = 1 / (t[1] - t[0]);

    QStringList code{"import numpy as np", "import matplotlib.pyplot as plt", "from scipy import signal, integrate", ""};
    code << m_source->python("t", "y") << "fs = 1 / (t[1] - t[0])  # Abtastfrequenz [Hz]" << "y_orig = y.copy()" << "";
    bool unitsChanged = false;
    QStringList applied;
    const QVariantList steps = this->steps();
    for (int i = 0; i < steps.size(); ++i) {
        const QVariantMap s = steps[i].toMap();
        if (!s.value("enabled", true).toBool()) continue;
        const QString type = s.value("type").toString();
        auto num = [&](const QString &key, const QString &label) {
            bool ok = true;
            QString message;
            const double v = evalNumber(s.value(key).toString(), &ok, NAN, &message);
            if (!ok || std::isnan(v)) fail(tr("Step %1, %2: %3").arg(i + 1).arg(label, ok ? tr("missing") : message));
            return v;
        };
        QString error;
        if (type == "butter") {
            const QString btype = s.value("btype").toString();
            const bool band = btype == "bandpass" || btype == "bandstop";
            const int order = int(num("order", tr("order")));
            const double f1 = num("f1", "f₁"), f2 = band ? num("f2", "f₂") : 0;
            if (!m_pendingError.isEmpty()) break;
            const num::Band b = btype == "highpass" ? num::Band::Highpass : btype == "bandpass" ? num::Band::Bandpass
                              : btype == "bandstop" ? num::Band::Bandstop : num::Band::Lowpass;
            const num::Sos sos = num::butter(order, f1, f2, b, fs, &error);
            if (!sos.empty()) y = num::sosfiltfilt(sos, y, &error);
            const QString wn = band ? QString("[%1, %2]").arg(pyNumber(f1), pyNumber(f2)) : pyNumber(f1);
            const QString pyType = btype == "bandpass" ? "band" : btype;
            code << QString("sos = signal.butter(N=%1, Wn=%2, btype='%3', fs=fs, output='sos')  # Butterworth").arg(order).arg(wn, pyType)
                 << "y = signal.sosfiltfilt(sos, y)";
            applied << QString("%1 %2 Hz").arg(btype, band ? fmt(f1) + "–" + fmt(f2) : fmt(f1));
        } else if (type == "savgol") {
            const int w = int(num("window", tr("window length"))), p = int(num("polyorder", tr("polynomial order")));
            if (!m_pendingError.isEmpty()) break;
            y = num::savgolFilter(y, w, p, &error);
            code << QString("y = signal.savgol_filter(y, window_length=%1, polyorder=%2)").arg(w).arg(p);
            applied << QString("Savitzky–Golay %1/%2").arg(w).arg(p);
        } else if (type == "medfilt") {
            int k = int(num("kernel", tr("kernel size")));
            if (!m_pendingError.isEmpty()) break;
            if (k % 2 == 0) error = tr("The kernel size must be odd");
            else y = num::medfilt(y, k);
            code << QString("y = signal.medfilt(y, %1)").arg(k);
            applied << tr("Median %1").arg(k);
        } else if (type == "detrend") {
            const bool linear = s.value("kind").toString() != "constant";
            y = num::detrend(y, linear);
            code << (linear ? "y = signal.detrend(y)" : "y = signal.detrend(y, type='constant')");
            applied << tr("detrend");
        } else if (type == "decimate") {
            const int q = int(num("q", tr("factor")));
            if (!m_pendingError.isEmpty()) break;
            y = num::decimate(y, q, &error);
            num::Vec tq;
            for (size_t k = 0; k < t.size(); k += size_t(std::max(1, q))) tq.push_back(t[k]);
            t = tq;
            fs /= q;
            code << QString("y = signal.decimate(y, %1)").arg(q) << QString("t = t[::%1]").arg(q) << QString("fs = fs / %1").arg(q);
            if (q > 13) code << "# SciPy empfiehlt bei q > 13 mehrere Aufrufe; Ergebnis entspricht aber genau diesem Aufruf";
            applied << tr("decimate %1").arg(q);
        } else if (type == "crop") {
            const double a = num("t1", "t₁"), b = num("t2", "t₂");
            if (!m_pendingError.isEmpty()) break;
            num::Vec tc, yc;
            for (size_t k = 0; k < t.size() && k < y.size(); ++k)
                if (t[k] > a && t[k] < b) {
                    tc.push_back(t[k]);
                    yc.push_back(y[k]);
                }
            if (tc.empty()) error = tr("No samples between %1 s and %2 s").arg(fmt(a), fmt(b));
            t = tc;
            y = yc;
            code << QString("bereich = (t > %1) & (t < %2)").arg(pyNumber(a), pyNumber(b)) << "t, y = t[bereich], y[bereich]";
            applied << QString("%1–%2 s").arg(fmt(a), fmt(b));
        } else if (type == "mean") {
            const double m = num::mean(y);
            for (double &v : y) v -= m;
            code << "y = y - y.mean()  # Gleichanteil entfernen";
            applied << tr("− mean");
        } else if (type == "scale") {
            const double f = num("factor", tr("factor"));
            if (!m_pendingError.isEmpty()) break;
            for (double &v : y) v *= f;
            code << QString("y = y * %1").arg(pyNumber(f));
            applied << QString("× %1").arg(fmt(f));
            unitsChanged = true;
        } else if (type == "gradient") {
            y = num::gradient(y, t);
            code << "y = np.gradient(y, t)  # Ableitung";
            applied << "d/dt";
            unitsChanged = true;
        } else if (type == "integrate") {
            y = num::cumulativeTrapezoid(y, t);
            if (!t.empty()) t.erase(t.begin());
            code << "y = integrate.cumulative_trapezoid(y, t)  # Integral (Trapezregel)" << "t = t[1:]";
            applied << "∫dt";
            unitsChanged = true;
        } else if (type == "abs") {
            for (double &v : y) v = std::abs(v);
            code << "y = np.abs(y)";
            applied << "|y|";
        }
        if (!error.isEmpty()) fail(tr("Step %1: %2").arg(i + 1).arg(error));
        if (!m_pendingError.isEmpty() || y.empty()) break;
    }
    if (y.size() != t.size()) t.resize(std::min(t.size(), y.size())), y.resize(t.size());
    const bool processed = !applied.isEmpty();

    heading(tr("Signal"));
    value("N", QString::number(y.size()));
    value("fs", fs, "Hz");
    value("Δt = 1/fs", 1 / fs, "s");
    if (!t.empty()) value(tr("Duration"), t.back() - t.front() + 1 / fs, "s");
    if (!y.empty()) {
        heading(processed ? tr("Values after %1").arg(applied.join(" → ")) : tr("Values"));
        const double mean = num::mean(y);
        num::Vec centered = y;
        for (double &v : centered) v -= mean;
        value(tr("Mean (offset)"), mean);
        value(tr("Standard deviation (ddof=0)"), num::stddev(y, 0));
        value(tr("RMS √(mean(y²))"), num::rms(y));
        value(tr("RMS without offset"), num::rms(centered));
        const auto mx = std::max_element(y.begin(), y.end()), mn = std::min_element(y.begin(), y.end());
        value(tr("Maximum"), QString("%1   (t = %2 s)").arg(fmt(*mx), fmt(t[size_t(mx - y.begin())])), pyNumber(*mx));
        value(tr("Minimum"), QString("%1   (t = %2 s)").arg(fmt(*mn), fmt(t[size_t(mn - y.begin())])), pyNumber(*mn));
        value(tr("Peak to peak"), *mx - *mn);
        value(tr("Integral ∫y dt (trapezoid)"), num::trapezoid(y, t));
        code << "" << "print('Mittelwert', y.mean())" << "print('RMS', np.sqrt(np.mean(np.square(y))))"
             << "print('RMS ohne Gleichanteil', np.sqrt(np.mean(np.square(y - y.mean()))))" << "print('Max', y.max(), 'bei t =', t[y.argmax()])"
             << "print('Integral', integrate.trapezoid(y, t))";
    }

    PlotSeries peakMarks;
    if (flag("peaks") && !y.empty()) {
        num::PeakOptions o;
        o.height = evalNumber(text("height"), nullptr, NAN);
        o.distance = evalNumber(text("distance"), nullptr, NAN);
        o.prominence = evalNumber(text("prominence"), nullptr, NAN);
        const auto peaks = num::findPeaks(y, o);
        QStringList args;
        if (!std::isnan(o.height)) args << "height=" + pyNumber(o.height);
        if (!std::isnan(o.distance)) args << "distance=" + pyNumber(o.distance);
        if (!std::isnan(o.prominence)) args << "prominence=" + pyNumber(o.prominence);
        code << "" << QString("peaks, _ = signal.find_peaks(%1)").arg((QStringList{"y"} + args).join(", "))
             << "print(t[peaks], y[peaks])";
        heading(tr("%n peaks (find_peaks)", "", int(peaks.size())));
        peakMarks.style = PlotSeries::Markers;
        peakMarks.color = QColor("#ff6b6b");
        peakMarks.markerSize = 8;
        peakMarks.label = tr("Peaks");
        for (size_t i = 0; i < peaks.size(); ++i) {
            const int p = peaks[i];
            peakMarks.x.push_back(t[size_t(p)]);
            peakMarks.y.push_back(y[size_t(p)]);
            m_peaks << QVariantMap{{"t", fmt(t[size_t(p)], 8)}, {"y", fmt(y[size_t(p)], 8)}};
            if (i < 30) value(QString("t = %1 s").arg(fmt(t[size_t(p)], 8)), y[size_t(p)]);
        }
        if (peaks.size() > 30) note(tr("… and %1 more; raise height, distance, or prominence").arg(peaks.size() - 30));
    }

    PlotSeries original, result;
    original.label = tr("Original");
    original.x = t0;
    original.y = y0;
    original.width = 1;
    original.color = QColor("#5ac8fa");
    original.opacity = processed ? 0.55 : 1;
    result.label = tr("Processed");
    result.x = t;
    result.y = y;
    result.width = 1.8;
    if (y.size() < 60 && y.size() < y0.size() / 4) result.style = PlotSeries::LineMarkers;

    std::vector<PlotAxes> axes;
    PlotAxes top;
    top.xLabel = m_source->xLabel();
    top.yLabel = m_source->yLabel();
    if (!processed) {
        top.series = {original};
        if (!peakMarks.x.empty()) top.series.push_back(peakMarks);
        axes = {top};
    } else if (unitsChanged) {
        top.series = {original};
        top.title = tr("Original");
        PlotAxes bottom;
        bottom.row = 1;
        bottom.xLabel = m_source->xLabel();
        bottom.title = applied.join(" → ");
        bottom.series = {result};
        if (!peakMarks.x.empty()) bottom.series.push_back(peakMarks);
        axes = {top, bottom};
    } else {
        if (flag("original")) top.series.push_back(original);
        top.series.push_back(result);
        if (!peakMarks.x.empty()) top.series.push_back(peakMarks);
        axes = {top};
    }
    m_figure.set(axes, int(axes.size()), 1);

    code << "" << "plt.figure(figsize=(12, 5))";
    if (processed && !unitsChanged) code << "plt.plot(t_orig, y_orig, 'b.', ms=2, label='Original')  # t_orig = Zeitarray vor der Bearbeitung";
    code << QString("plt.plot(t, y, 'r', lw=2, label='%1')").arg(processed ? "bearbeitet" : "Signal");
    if (!peakMarks.x.empty()) code << "plt.plot(t[peaks], y[peaks], 'ro', label='Peaks')";
    code << "plt.xlabel('Zeit t [s]')" << "plt.ylabel('Signal y')" << "plt.grid()" << "plt.legend()" << "plt.show()";
    QString joined = code.join('\n') + '\n';
    joined.replace("y_orig = y.copy()", "t_orig, y_orig = t.copy(), y.copy()");
    publish(joined);
}
