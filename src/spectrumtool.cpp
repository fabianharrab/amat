#include "tools.h"

#include <algorithm>
#include <cmath>

SpectrumTool::SpectrumTool(QObject *parent) : Tool(parent) {
    setSource(new DataSource(true, this));
    setDefaults({{"window", "boxcar"}, {"offset", true}, {"doubling", "all"}, {"n", ""}, {"logy", false},
                 {"fmax", ""}, {"peaks", "5"}, {"height", ""}});
}

void SpectrumTool::compute() {
    if (!m_source->error().isEmpty() || m_source->y().empty()) {
        fail(m_source->error().isEmpty() ? tr("No data") : m_source->error());
        m_figure.clear(m_pendingError);
        publish({});
        return;
    }
    const num::Vec &t = m_source->x();
    const num::Vec &y = m_source->y();
    const int N = int(y.size());
    double fs = m_source->fs();
    if (!std::isfinite(fs) && t.size() > 1) fs = 1 / (t[1] - t[0]);
    if (!(fs > 0)) {
        fail(tr("The sampling rate fs is unknown"));
        m_figure.clear(m_pendingError);
        return;
    }
    const double nValue = number("n", tr("FFT length n"), N);
    const int n = std::isnan(nValue) ? N : std::max(2, int(nValue));
    const QString windowName = text("window").isEmpty() ? "boxcar" : text("window");
    const bool keepOffset = flag("offset");
    const bool doubleNyquist = text("doubling") != "nyquist";

    // The course's FFT workflow, step by step.
    const num::Vec w = num::window(windowName, N);
    const double mean = num::mean(y);
    num::Vec yw(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        yw[size_t(i)] = keepOffset ? w[size_t(i)] * (y[size_t(i)] - mean) + mean : w[size_t(i)] * y[size_t(i)];
    const auto Y = num::rfft(yw, n);
    const double divisor = std::min(n, N);
    num::Vec A(Y.size());
    for (size_t k = 0; k < Y.size(); ++k) A[k] = std::abs(Y[k]) / divisor;
    const size_t last = doubleNyquist ? A.size() : (n % 2 == 0 ? A.size() - 1 : A.size());
    for (size_t k = 1; k < last; ++k) A[k] *= 2;
    const num::Vec f = num::rfftfreq(n, 1 / fs);

    heading(tr("Sampling"));
    value("N", QString::number(N));
    if (n != N) value(tr("FFT length n"), QString::number(n) + (n > N ? tr(" (zero padded)") : tr(" (cropped)")));
    value("fs", fs, "Hz");
    value("Δt = 1/fs", 1 / fs, "s");
    value(tr("Measuring time t_meas = N/fs"), N / fs, "s");
    value(tr("Frequency resolution Δf = fs/n"), fs / n, "Hz");
    value(tr("Highest frequency f_max = fs/2 (Nyquist)"), fs / 2, "Hz");
    heading(tr("Spectrum"));
    value(tr("DC component A(0 Hz)"), A[0]);

    // Either every peak above a height (find_peaks(A, height=...)) or the
    // largest few. find_peaks never reports the DC bin at index 0.
    const int wanted = std::clamp(int(evalNumber(text("peaks"), nullptr, 5)), 0, 50);
    const double height = evalNumber(text("height"), nullptr, NAN);
    num::PeakOptions options;
    options.height = height;
    std::vector<int> peaks = num::findPeaks(A, options);
    if (std::isnan(height)) {
        std::sort(peaks.begin(), peaks.end(), [&](int a, int b) { return A[size_t(a)] > A[size_t(b)]; });
        if (int(peaks.size()) > wanted) peaks.resize(size_t(wanted));
    } else if (peaks.size() > 100) {
        peaks.resize(100);
    }
    PlotSeries marks;
    marks.style = PlotSeries::Markers;
    marks.color = QColor("#ff6b6b");
    marks.markerSize = 7;
    marks.label = std::isnan(height) ? tr("Largest peaks") : tr("Peaks with A ≥ %1").arg(fmt(height));
    if (!std::isnan(height)) {
        heading(tr("%n peaks with A ≥ %1", "", int(peaks.size())).arg(fmt(height)));
        QStringList freqs;
        for (int p : peaks) freqs << QLocale().toString(f[size_t(p)], 'f', 3);
        if (!peaks.empty()) answer(tr("Frequencies [Hz], 3 decimals"), freqs.join(",  "));
    }
    for (size_t i = 0; i < peaks.size(); ++i) {
        const size_t p = size_t(peaks[i]);
        value(tr("Peak %1").arg(i + 1), QString("f = %1 Hz    A = %2").arg(fmt(f[p], 7), fmt(A[p], 5)), pyNumber(f[p]));
        marks.x.push_back(f[p]);
        marks.y.push_back(A[p]);
    }
    if (peaks.empty()) note(tr("No peaks besides the DC component"));

    PlotAxes time, spectrum;
    time.xLabel = m_source->xLabel();
    time.yLabel = m_source->yLabel();
    time.title = tr("Time signal");
    PlotSeries original;
    original.label = tr("Original signal");
    original.x = t;
    original.y = y;
    original.width = 1.2;
    original.color = QColor("#5ac8fa");
    time.series.push_back(original);
    if (windowName != "boxcar") {
        PlotSeries windowed = original;
        windowed.label = tr("With %1 window").arg(windowName);
        windowed.y = yw;
        windowed.color = QColor();
        time.series.push_back(windowed);
    }
    spectrum.row = 1;
    spectrum.title = tr("Amplitude spectrum");
    spectrum.xLabel = tr("Frequency f [Hz]");
    spectrum.yLabel = tr("Amplitude A");
    spectrum.logY = flag("logy");
    PlotSeries amp;
    amp.x = f;
    amp.y = A;
    amp.label = tr("Spectrum");
    if (A.size() <= 200) amp.style = PlotSeries::LineMarkers;
    spectrum.series.push_back(amp);
    if (!marks.x.empty()) spectrum.series.push_back(marks);
    const double fmax = evalNumber(text("fmax"), nullptr, NAN);
    if (fmax > 0) {
        spectrum.hasXLimits = true;
        spectrum.xMin = 0;
        spectrum.xMax = fmax;
    }
    m_figure.set({time, spectrum}, 2, 1);

    QStringList code{"import numpy as np", "import matplotlib.pyplot as plt", "from scipy import fft, signal", ""};
    code << "# 1. Zeitsignal, Abtastfrequenz und Sampleanzahl" << m_source->python("t", "y") << QString("fs = %1  # Abtastfrequenz [Hz]").arg(pyNumber(fs))
         << "N = len(y)  # Sampleanzahl" << "" << "# 2. Fensterfunktion";
    if (windowName == "boxcar") code << "y_w = y  # keine Fensterung ('boxcar')";
    else {
        code << QString("w = signal.windows.get_window('%1', N)").arg(windowName);
        code << (keepOffset ? "y_w = w * (y - y.mean()) + y.mean()  # Offset fuer die Fensterung entfernen" : "y_w = w * y");
    }
    code << "" << "# 3. FFT berechnen und Amplituden skalieren";
    if (n != N) code << QString("n = %1").arg(n) << "Y = fft.rfft(y_w, n) / min(n, N)";
    else code << "Y = fft.rfft(y_w) / N";
    code << "A = np.abs(Y)" << (doubleNyquist ? "A[1:] *= 2  # alle ausser Gleichanteil verdoppeln" : "A[1:-1] *= 2  # ausser Gleichanteil und Nyquist-Frequenz")
         << "" << "# 4. Frequenzachse" << (n != N ? "f = fft.rfftfreq(n, 1/fs)" : "f = fft.rfftfreq(N, 1/fs)") << ""
         << "# 5. Darstellen" << "fig, axs = plt.subplots(nrows=2, figsize=(12, 7))" << "axs[0].plot(t, y, 'c', label='Originalsignal')";
    if (windowName != "boxcar") code << "axs[0].plot(t, y_w, 'b', label='Signal mit Fensterung')";
    code << "axs[0].set_xlabel('Zeit $t$ [s]')" << "axs[0].set_ylabel('Signal $y$')" << "axs[0].legend()" << "axs[0].grid()"
         << (A.size() <= 200 ? "axs[1].plot(f, A, 'ro-')" : "axs[1].plot(f, A, 'r-')");
    if (flag("logy")) code << "axs[1].set_yscale('log')";
    if (fmax > 0) code << QString("axs[1].set_xlim(0, %1)").arg(pyNumber(fmax));
    code << "axs[1].set_xlabel('Frequenz $f$ [Hz]')" << "axs[1].set_ylabel('Amplitude $A$')" << "axs[1].grid()" << "plt.show()" << ""
         << (std::isnan(height)
                 ? QString("peaks, _ = signal.find_peaks(A)\ngroesste = peaks[np.argsort(A[peaks])[::-1][:%1]]\nprint(f[groesste], A[groesste])").arg(wanted)
                 : QString("peaks, _ = signal.find_peaks(A, height=%1)  # Index 0 (DC) wird nie als Peak erkannt\nprint(np.round(f[peaks], 3))").arg(pyNumber(height)));
    publish(code.join('\n') + '\n');
}
