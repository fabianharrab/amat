#pragma once

#include <QString>
#include <QStringList>
#include <array>
#include <complex>
#include <functional>
#include <vector>

// The NumPy and SciPy routines the course uses, reimplemented so results
// match what a notebook prints (same defaults, same edge handling). Each
// function names its Python counterpart.
namespace num {

using Vec = std::vector<double>;
using Complex = std::complex<double>;

// --- Statistics -----------------------------------------------------------
double sum(const Vec &x);
double mean(const Vec &x);
double stddev(const Vec &x, int ddof = 1);      // x.std(ddof=1)
double percentile(Vec x, double q);             // np.percentile(x, q), linear
double median(const Vec &x);
double rms(const Vec &x);                       // np.sqrt(np.mean(np.square(x)))
double minimum(const Vec &x);
double maximum(const Vec &x);

struct Histogram {
    Vec edges;  // bins + 1
    Vec counts;
};
Histogram histogram(const Vec &x, int bins, bool density = false); // np.histogram

struct BoxStats {
    double q1 = 0, median = 0, q3 = 0, whiskerLow = 0, whiskerHigh = 0;
    Vec outliers;
};
BoxStats boxStats(const Vec &x); // plt.boxplot, whis=1.5

// --- Spectrum -------------------------------------------------------------
std::vector<Complex> rfft(const Vec &x, int n = -1);  // scipy.fft.rfft(x, n)
Vec rfftfreq(int n, double d);                       // scipy.fft.rfftfreq
// signal.windows.get_window(name, n): periodic ("fftbins") windows.
Vec window(const QString &name, int n);
QStringList windowNames();

// --- Filters --------------------------------------------------------------
using Section = std::array<double, 6>; // b0 b1 b2 a0 a1 a2
using Sos = std::vector<Section>;

enum class Band { Lowpass, Highpass, Bandpass, Bandstop };
// signal.butter(order, Wn, btype, fs=fs, output='sos'); f2 only for bands.
Sos butter(int order, double f1, double f2, Band band, double fs, QString *error = nullptr);
// signal.cheby1(order, rp, Wn) with Wn normalized to Nyquist, lowpass.
Sos cheby1Lowpass(int order, double rippleDb, double wn);
Vec sosfilt(const Sos &sos, const Vec &x);
Vec sosfiltfilt(const Sos &sos, const Vec &x, QString *error = nullptr);

Vec savgolFilter(const Vec &x, int window, int polyorder, QString *error = nullptr); // mode='interp'
Vec medfilt(const Vec &x, int kernel);                                                // zero padded
Vec detrend(const Vec &x, bool linear = true);                                         // signal.detrend
Vec decimate(const Vec &x, int q, QString *error = nullptr);                            // iir, zero_phase

struct PeakOptions {
    double height = NAN;     // minimum height
    double distance = NAN;   // minimum distance in samples
    double prominence = NAN; // minimum prominence
};
std::vector<int> findPeaks(const Vec &x, const PeakOptions &options); // signal.find_peaks
Vec peakProminences(const Vec &x, const std::vector<int> &peaks);

// --- Calculus -------------------------------------------------------------
Vec gradient(const Vec &y, const Vec &x);          // np.gradient(y, x)
double trapezoid(const Vec &y, const Vec &x);      // integrate.trapezoid
Vec cumulativeTrapezoid(const Vec &y, const Vec &x); // integrate.cumulative_trapezoid (N-1)

// --- Interpolation & fitting ----------------------------------------------
double interp(double xi, const Vec &x, const Vec &y); // np.interp
class CubicSpline {                                    // interpolate.CubicSpline, not-a-knot
public:
    bool fit(const Vec &x, const Vec &y, QString *error = nullptr);
    double operator()(double xi) const;

private:
    Vec m_x, m_y, m_m; // second derivatives
};

struct LinearRegression {
    double slope = 0, intercept = 0, r = 0, slopeStderr = 0, interceptStderr = 0;
};
LinearRegression linregress(const Vec &x, const Vec &y); // stats.linregress
Vec polyfit(const Vec &x, const Vec &y, int degree, QString *error = nullptr); // highest power first
double polyval(const Vec &p, double x);

struct FitResult {
    Vec params, stderrs;
    double residual = 0; // sum of squared residuals
    bool ok = false;
    QString error;
};
// optimize.curve_fit(model, x, y, p0) via Levenberg–Marquardt.
FitResult curveFit(const std::function<double(double, const Vec &)> &model, const Vec &x, const Vec &y, Vec p0);

// Least squares A·p ≈ b by Householder QR; A is rows × cols, row-major.
Vec leastSquares(std::vector<Vec> A, Vec b, QString *error = nullptr);

// --- Scalar functions -----------------------------------------------------
// Roots of f in [a, b]: scans `samples` intervals for sign changes, then
// refines each by Brent's method (like optimize.root_scalar with a bracket).
Vec roots(const std::function<double(double)> &f, double a, double b, int samples = 4000);
struct Extremum {
    double x, y;
    bool maximum;
};
std::vector<Extremum> extrema(const std::function<double(double)> &f, double a, double b, int samples = 4000);
double integrate(const std::function<double(double)> &f, double a, double b); // adaptive Simpson

// --- Vectors & matrices ---------------------------------------------------
using Matrix = std::vector<Vec>;
double determinant(Matrix m);
bool inverse(const Matrix &m, Matrix *out);

// Rounds per the course rule: U to two significant digits and the value to
// the same decimal place. Returns the number of decimals used.
int uncertaintyDecimals(double U, int significant = 2);

} // namespace num
