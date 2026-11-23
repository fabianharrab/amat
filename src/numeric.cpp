#include "numeric.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace num {

// --- Statistics -----------------------------------------------------------

double sum(const Vec &x) {
    // Pairwise-ish accuracy without the cost: Kahan summation.
    double s = 0, c = 0;
    for (double v : x) {
        const double y = v - c, t = s + y;
        c = (t - s) - y;
        s = t;
    }
    return s;
}

double mean(const Vec &x) { return x.empty() ? NAN : sum(x) / double(x.size()); }

double stddev(const Vec &x, int ddof) {
    if (int(x.size()) <= ddof) return NAN;
    const double m = mean(x);
    double s = 0;
    for (double v : x) s += (v - m) * (v - m);
    return std::sqrt(s / double(int(x.size()) - ddof));
}

double percentile(Vec x, double q) {
    if (x.empty()) return NAN;
    std::sort(x.begin(), x.end());
    const double index = q / 100.0 * double(x.size() - 1);
    const auto lo = size_t(std::floor(index));
    const size_t hi = std::min(lo + 1, x.size() - 1);
    const double fraction = index - double(lo);
    return x[lo] + (x[hi] - x[lo]) * fraction;
}

double median(const Vec &x) { return percentile(x, 50); }

double rms(const Vec &x) {
    if (x.empty()) return NAN;
    double s = 0;
    for (double v : x) s += v * v;
    return std::sqrt(s / double(x.size()));
}

double minimum(const Vec &x) { return x.empty() ? NAN : *std::min_element(x.begin(), x.end()); }
double maximum(const Vec &x) { return x.empty() ? NAN : *std::max_element(x.begin(), x.end()); }

Histogram histogram(const Vec &x, int bins, bool density) {
    Histogram h;
    if (x.empty() || bins < 1) return h;
    double lo = minimum(x), hi = maximum(x);
    if (lo == hi) {
        lo -= 0.5;
        hi += 0.5;
    }
    h.edges.resize(bins + 1);
    for (int i = 0; i <= bins; ++i) h.edges[i] = lo + (hi - lo) * i / bins;
    h.counts.assign(bins, 0);
    for (double v : x) {
        int i = int(std::floor((v - lo) / (hi - lo) * bins));
        i = std::clamp(i, 0, bins - 1);
        h.counts[i] += 1;
    }
    if (density) {
        const double width = (hi - lo) / bins;
        for (double &c : h.counts) c /= double(x.size()) * width;
    }
    return h;
}

BoxStats boxStats(const Vec &x) {
    BoxStats b;
    if (x.empty()) return b;
    b.q1 = percentile(x, 25);
    b.median = percentile(x, 50);
    b.q3 = percentile(x, 75);
    const double iqr = b.q3 - b.q1;
    const double lo = b.q1 - 1.5 * iqr, hi = b.q3 + 1.5 * iqr;
    b.whiskerLow = b.q1;
    b.whiskerHigh = b.q3;
    double wl = INFINITY, wh = -INFINITY;
    for (double v : x) {
        if (v >= lo) wl = std::min(wl, v);
        if (v <= hi) wh = std::max(wh, v);
        if (v < lo || v > hi) b.outliers.push_back(v);
    }
    if (std::isfinite(wl)) b.whiskerLow = std::min(wl, b.q1);
    if (std::isfinite(wh)) b.whiskerHigh = std::max(wh, b.q3);
    return b;
}

// --- FFT ------------------------------------------------------------------

namespace {

void fftRadix2(std::vector<Complex> &a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = 2 * M_PI / double(len) * (inverse ? 1 : -1);
        const Complex wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            Complex w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                // Recompute twiddles periodically to keep rounding error low on long transforms.
                if ((j & 63) == 0) w = std::polar(1.0, angle * double(j));
                const Complex u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto &v : a) v /= double(n);
}

// Any length via Bluestein's chirp-z algorithm.
std::vector<Complex> fft(std::vector<Complex> x) {
    const size_t n = x.size();
    if (n <= 1) return x;
    if ((n & (n - 1)) == 0) {
        fftRadix2(x, false);
        return x;
    }
    size_t m = 1;
    while (m < 2 * n - 1) m <<= 1;
    std::vector<Complex> w(n), a(m), b(m);
    const unsigned long long twoN = 2ULL * n;
    for (size_t k = 0; k < n; ++k) {
        const unsigned long long k2 = (unsigned long long)k * k % twoN;
        w[k] = std::polar(1.0, -M_PI * double(k2) / double(n));
    }
    for (size_t k = 0; k < n; ++k) a[k] = x[k] * w[k];
    b[0] = std::conj(w[0]);
    for (size_t k = 1; k < n; ++k) b[k] = b[m - k] = std::conj(w[k]);
    fftRadix2(a, false);
    fftRadix2(b, false);
    for (size_t i = 0; i < m; ++i) a[i] *= b[i];
    fftRadix2(a, true);
    for (size_t k = 0; k < n; ++k) x[k] = a[k] * w[k];
    return x;
}

} // namespace

std::vector<Complex> rfft(const Vec &x, int n) {
    if (n < 0) n = int(x.size());
    std::vector<Complex> c(n);
    for (int i = 0; i < n && i < int(x.size()); ++i) c[i] = x[i];
    auto full = fft(std::move(c));
    full.resize(n / 2 + 1);
    return full;
}

Vec rfftfreq(int n, double d) {
    Vec f(n / 2 + 1);
    for (int i = 0; i < int(f.size()); ++i) f[i] = i / (n * d);
    return f;
}

QStringList windowNames() { return {"boxcar", "hann", "hamming", "blackman", "bartlett", "flattop", "tukey"}; }

namespace {
Vec generalCosine(int m, const std::vector<double> &a) {
    Vec w(m, 0.0);
    for (int i = 0; i < m; ++i) {
        const double fac = m == 1 ? 0 : -M_PI + 2 * M_PI * i / (m - 1);
        for (size_t k = 0; k < a.size(); ++k) w[i] += a[k] * std::cos(double(k) * fac);
    }
    return w;
}

Vec symmetricWindow(const QString &name, int m) {
    if (m == 1) return {1.0};
    if (name == "hann") return generalCosine(m, {0.5, 0.5});
    if (name == "hamming") return generalCosine(m, {0.54, 0.46});
    if (name == "blackman") return generalCosine(m, {0.42, 0.50, 0.08});
    if (name == "flattop") return generalCosine(m, {0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368});
    if (name == "bartlett") {
        Vec w(m);
        for (int i = 0; i < m; ++i)
            w[i] = i <= (m - 1) / 2.0 ? 2.0 * i / (m - 1) : 2.0 - 2.0 * i / (m - 1);
        return w;
    }
    if (name == "tukey") {
        const double alpha = 0.5;
        Vec w(m, 1.0);
        const int width = int(std::floor(alpha * (m - 1) / 2.0));
        for (int i = 0; i <= width; ++i) w[i] = 0.5 * (1 + std::cos(M_PI * (-1 + 2.0 * i / alpha / (m - 1))));
        for (int i = m - width - 1; i < m; ++i)
            w[i] = 0.5 * (1 + std::cos(M_PI * (-2.0 / alpha + 1 + 2.0 * i / alpha / (m - 1))));
        return w;
    }
    return Vec(m, 1.0);
}
} // namespace

Vec window(const QString &name, int n) {
    if (n <= 0) return {};
    // get_window returns periodic windows: a symmetric one of n + 1 points
    // with the last dropped.
    Vec w = symmetricWindow(name, n + 1);
    w.resize(n);
    return w;
}

// --- Filter design --------------------------------------------------------

namespace {

using CVec = std::vector<Complex>;

Complex product(const CVec &v) {
    Complex p(1);
    for (const auto &c : v) p *= c;
    return p;
}

struct Zpk {
    CVec z, p;
    double k = 1;
};

Zpk buttap(int n) {
    Zpk s;
    for (int m = -n + 1; m < n; m += 2) s.p.push_back(-std::exp(Complex(0, M_PI * m / (2.0 * n))));
    return s;
}

Zpk cheb1ap(int n, double rp) {
    Zpk s;
    const double eps = std::sqrt(std::pow(10.0, 0.1 * rp) - 1.0);
    const double mu = 1.0 / n * std::asinh(1 / eps);
    for (int m = -n + 1; m < n; m += 2) {
        const double theta = M_PI * m / (2.0 * n);
        s.p.push_back(-std::sinh(Complex(mu, theta)));
    }
    // The gain is prod(-p).
    Complex g(1);
    for (auto &p : s.p) g *= -p;
    s.k = g.real();
    if (n % 2 == 0) s.k /= std::sqrt(1 + eps * eps);
    return s;
}

Zpk transform(const Zpk &proto, Band band, double w1, double w2) {
    Zpk s;
    const int degree = int(proto.p.size() - proto.z.size());
    switch (band) {
    case Band::Lowpass:
        for (auto z : proto.z) s.z.push_back(z * w1);
        for (auto p : proto.p) s.p.push_back(p * w1);
        s.k = proto.k * std::pow(w1, degree);
        break;
    case Band::Highpass:
        for (auto z : proto.z) s.z.push_back(w1 / z);
        for (auto p : proto.p) s.p.push_back(w1 / p);
        for (int i = 0; i < degree; ++i) s.z.push_back(0);
        {
            CVec nz, np;
            for (auto z : proto.z) nz.push_back(-z);
            for (auto p : proto.p) np.push_back(-p);
            s.k = proto.k * (product(nz) / product(np)).real();
        }
        break;
    case Band::Bandpass: {
        const double wo = std::sqrt(w1 * w2), bw = w2 - w1;
        for (auto z : proto.z) {
            const Complex zl = z * bw / 2.0, r = std::sqrt(zl * zl - wo * wo);
            s.z.push_back(zl + r);
            s.z.push_back(zl - r);
        }
        for (auto p : proto.p) {
            const Complex pl = p * bw / 2.0, r = std::sqrt(pl * pl - wo * wo);
            s.p.push_back(pl + r);
            s.p.push_back(pl - r);
        }
        for (int i = 0; i < degree; ++i) s.z.push_back(0);
        s.k = proto.k * std::pow(bw, degree);
        break;
    }
    case Band::Bandstop: {
        const double wo = std::sqrt(w1 * w2), bw = w2 - w1;
        for (auto z : proto.z) {
            const Complex zh = (bw / 2.0) / z, r = std::sqrt(zh * zh - wo * wo);
            s.z.push_back(zh + r);
            s.z.push_back(zh - r);
        }
        for (auto p : proto.p) {
            const Complex ph = (bw / 2.0) / p, r = std::sqrt(ph * ph - wo * wo);
            s.p.push_back(ph + r);
            s.p.push_back(ph - r);
        }
        for (int i = 0; i < degree; ++i) {
            s.z.push_back(Complex(0, wo));
            s.z.push_back(Complex(0, -wo));
        }
        CVec nz, np;
        for (auto z : proto.z) nz.push_back(-z);
        for (auto p : proto.p) np.push_back(-p);
        s.k = proto.k * (product(nz) / product(np)).real();
        break;
    }
    }
    return s;
}

Zpk bilinear(const Zpk &a, double fs) {
    Zpk d;
    const double fs2 = 2 * fs;
    const int degree = int(a.p.size() - a.z.size());
    CVec num, den;
    for (auto z : a.z) {
        d.z.push_back((fs2 + z) / (fs2 - z));
        num.push_back(fs2 - z);
    }
    for (auto p : a.p) {
        d.p.push_back((fs2 + p) / (fs2 - p));
        den.push_back(fs2 - p);
    }
    for (int i = 0; i < degree; ++i) d.z.push_back(-1);
    d.k = a.k * (product(num) / product(den)).real();
    return d;
}

// Groups roots into conjugate pairs and pairs of reals; a leftover real
// root forms a first-order group.
std::vector<CVec> groupRoots(CVec roots) {
    std::vector<CVec> groups;
    CVec reals, complexes;
    for (auto r : roots) {
        if (std::abs(r.imag()) < 1e-10 * std::max(1.0, std::abs(r))) reals.push_back(Complex(r.real(), 0));
        else if (r.imag() > 0) complexes.push_back(r);
    }
    // Poles nearest the unit circle last, the way zpk2sos orders sections.
    std::sort(complexes.begin(), complexes.end(),
              [](Complex a, Complex b) { return std::abs(1.0 - std::abs(a)) > std::abs(1.0 - std::abs(b)); });
    std::sort(reals.begin(), reals.end(), [](Complex a, Complex b) { return a.real() < b.real(); });
    for (auto c : complexes) groups.push_back({c, std::conj(c)});
    for (size_t i = 0; i + 1 < reals.size(); i += 2) groups.push_back({reals[i], reals[i + 1]});
    if (reals.size() % 2) groups.push_back({reals.back()});
    return groups;
}

Sos zpk2sos(const Zpk &d) {
    auto poleGroups = groupRoots(d.p);
    auto zeroGroups = groupRoots(d.z);
    // Put first-order groups at the end of both lists so they meet.
    auto byOrder = [](const CVec &a, const CVec &b) { return a.size() > b.size(); };
    std::stable_sort(poleGroups.begin(), poleGroups.end(), byOrder);
    std::stable_sort(zeroGroups.begin(), zeroGroups.end(), byOrder);
    while (zeroGroups.size() < poleGroups.size()) zeroGroups.push_back({});
    Sos sos;
    for (size_t i = 0; i < poleGroups.size(); ++i) {
        auto poly = [](const CVec &r) {
            std::array<double, 3> c{1, 0, 0};
            if (r.size() == 1) c = {1, -r[0].real(), 0};
            if (r.size() == 2) c = {1, -(r[0] + r[1]).real(), (r[0] * r[1]).real()};
            return c;
        };
        const auto b = poly(zeroGroups[i]);
        const auto a = poly(poleGroups[i]);
        sos.push_back({b[0], b[1], b[2], a[0], a[1], a[2]});
    }
    if (!sos.empty())
        for (int j = 0; j < 3; ++j) sos[0][j] *= d.k;
    return sos;
}

} // namespace

Sos butter(int order, double f1, double f2, Band band, double fs, QString *error) {
    if (order < 1 || order > 30) {
        if (error) *error = QObject::tr("Filter order must be between 1 and 30");
        return {};
    }
    const double nyquist = fs / 2;
    const double wn1 = f1 / nyquist, wn2 = f2 / nyquist;
    const bool twoEdges = band == Band::Bandpass || band == Band::Bandstop;
    if (!(fs > 0) || !(wn1 > 0 && wn1 < 1) || (twoEdges && !(wn2 > wn1 && wn2 < 1))) {
        if (error)
            *error = twoEdges ? QObject::tr("Cutoff frequencies must satisfy 0 < f1 < f2 < fs/2 (%1 Hz)").arg(nyquist)
                              : QObject::tr("Cutoff frequency must be between 0 and fs/2 (%1 Hz)").arg(nyquist);
        return {};
    }
    // Pre-warp for the bilinear transform with fs = 2 (normalized design).
    const double w1 = 4 * std::tan(M_PI * wn1 / 2), w2 = 4 * std::tan(M_PI * wn2 / 2);
    return zpk2sos(bilinear(transform(buttap(order), band, w1, w2), 2.0));
}

Sos cheby1Lowpass(int order, double rippleDb, double wn) {
    const double w = 4 * std::tan(M_PI * wn / 2);
    return zpk2sos(bilinear(transform(cheb1ap(order, rippleDb), Band::Lowpass, w, 0), 2.0));
}

namespace {

Vec sosfiltState(const Sos &sos, const Vec &x, std::vector<std::array<double, 2>> zi) {
    Vec y = x;
    for (size_t s = 0; s < sos.size(); ++s) {
        const auto &c = sos[s];
        const double b0 = c[0] / c[3], b1 = c[1] / c[3], b2 = c[2] / c[3], a1 = c[4] / c[3], a2 = c[5] / c[3];
        double z0 = zi[s][0], z1 = zi[s][1];
        for (double &v : y) {
            const double in = v;
            const double out = b0 * in + z0;
            z0 = b1 * in - a1 * out + z1;
            z1 = b2 * in - a2 * out;
            v = out;
        }
    }
    return y;
}

// signal.sosfilt_zi: per-section steady state for a unit step.
std::vector<std::array<double, 2>> sosfiltZi(const Sos &sos) {
    std::vector<std::array<double, 2>> zi(sos.size());
    double scale = 1;
    for (size_t s = 0; s < sos.size(); ++s) {
        const auto &c = sos[s];
        const double b0 = c[0] / c[3], b1 = c[1] / c[3], b2 = c[2] / c[3], a1 = c[4] / c[3], a2 = c[5] / c[3];
        // Solve [[1+a1, -1], [a2, 1]] z = [b1 - a1 b0, b2 - a2 b0].
        const double r0 = b1 - a1 * b0, r1 = b2 - a2 * b0;
        const double det = (1 + a1) + a2;
        const double z0 = (r0 + r1) / det;
        const double z1 = r1 - a2 * z0;
        zi[s] = {z0 * scale, z1 * scale};
        scale *= (b0 + b1 + b2) / (1 + a1 + a2);
    }
    return zi;
}

} // namespace

Vec sosfilt(const Sos &sos, const Vec &x) {
    return sosfiltState(sos, x, std::vector<std::array<double, 2>>(sos.size(), {0, 0}));
}

Vec sosfiltfilt(const Sos &sos, const Vec &x, QString *error) {
    int ntaps = 2 * int(sos.size()) + 1;
    int zeroB2 = 0, zeroA2 = 0;
    for (const auto &s : sos) {
        zeroB2 += s[2] == 0;
        zeroA2 += s[5] == 0;
    }
    ntaps -= std::min(zeroB2, zeroA2);
    const int padlen = 3 * ntaps;
    const int n = int(x.size());
    if (n <= padlen) {
        if (error) *error = QObject::tr("The signal needs more than %1 samples for this filter").arg(padlen);
        return {};
    }
    // Odd extension at both ends, as padtype='odd'.
    Vec ext;
    ext.reserve(n + 2 * padlen);
    for (int i = padlen; i >= 1; --i) ext.push_back(2 * x[0] - x[i]);
    ext.insert(ext.end(), x.begin(), x.end());
    for (int i = n - 2; i >= n - 1 - padlen; --i) ext.push_back(2 * x[n - 1] - x[i]);

    const auto zi = sosfiltZi(sos);
    auto scaled = [&](double v) {
        auto z = zi;
        for (auto &s : z) {
            s[0] *= v;
            s[1] *= v;
        }
        return z;
    };
    Vec y = sosfiltState(sos, ext, scaled(ext.front()));
    std::reverse(y.begin(), y.end());
    y = sosfiltState(sos, y, scaled(y.front()));
    std::reverse(y.begin(), y.end());
    return Vec(y.begin() + padlen, y.end() - padlen);
}

// --- Linear algebra -------------------------------------------------------

Vec leastSquares(std::vector<Vec> A, Vec b, QString *error) {
    const int m = int(A.size());
    const int n = m ? int(A[0].size()) : 0;
    if (m < n || n == 0) {
        if (error) *error = QObject::tr("Not enough data points");
        return {};
    }
    // Scale columns to unit norm for conditioning, as numpy.polyfit does.
    Vec scale(n, 1.0);
    for (int j = 0; j < n; ++j) {
        double s = 0;
        for (int i = 0; i < m; ++i) s += A[i][j] * A[i][j];
        scale[j] = s > 0 ? std::sqrt(s) : 1.0;
        for (int i = 0; i < m; ++i) A[i][j] /= scale[j];
    }
    // Householder QR, applied to b as we go.
    for (int k = 0; k < n; ++k) {
        double norm = 0;
        for (int i = k; i < m; ++i) norm += A[i][k] * A[i][k];
        norm = std::sqrt(norm);
        if (norm < 1e-300) {
            if (error) *error = QObject::tr("The system is singular");
            return {};
        }
        const double alpha = A[k][k] > 0 ? -norm : norm;
        Vec v(m - k);
        for (int i = k; i < m; ++i) v[i - k] = A[i][k];
        v[0] -= alpha;
        double vnorm = 0;
        for (double c : v) vnorm += c * c;
        if (vnorm < 1e-300) continue;
        for (int j = k; j < n; ++j) {
            double dot = 0;
            for (int i = k; i < m; ++i) dot += v[i - k] * A[i][j];
            const double f = 2 * dot / vnorm;
            for (int i = k; i < m; ++i) A[i][j] -= f * v[i - k];
        }
        double dot = 0;
        for (int i = k; i < m; ++i) dot += v[i - k] * b[i];
        const double f = 2 * dot / vnorm;
        for (int i = k; i < m; ++i) b[i] -= f * v[i - k];
    }
    Vec x(n);
    for (int k = n - 1; k >= 0; --k) {
        double s = b[k];
        for (int j = k + 1; j < n; ++j) s -= A[k][j] * x[j];
        if (std::abs(A[k][k]) < 1e-14) {
            if (error) *error = QObject::tr("The system is singular");
            return {};
        }
        x[k] = s / A[k][k];
    }
    for (int j = 0; j < n; ++j) x[j] /= scale[j];
    return x;
}

double determinant(Matrix m) {
    const int n = int(m.size());
    double det = 1;
    for (int c = 0; c < n; ++c) {
        int pivot = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs(m[r][c]) > std::abs(m[pivot][c])) pivot = r;
        if (m[pivot][c] == 0) return 0;
        if (pivot != c) {
            std::swap(m[pivot], m[c]);
            det = -det;
        }
        det *= m[c][c];
        for (int r = c + 1; r < n; ++r) {
            const double f = m[r][c] / m[c][c];
            for (int k = c; k < n; ++k) m[r][k] -= f * m[c][k];
        }
    }
    return det;
}

bool inverse(const Matrix &input, Matrix *out) {
    const int n = int(input.size());
    Matrix m = input;
    Matrix inv(n, Vec(n, 0.0));
    for (int i = 0; i < n; ++i) inv[i][i] = 1;
    for (int c = 0; c < n; ++c) {
        int pivot = c;
        for (int r = c + 1; r < n; ++r)
            if (std::abs(m[r][c]) > std::abs(m[pivot][c])) pivot = r;
        if (std::abs(m[pivot][c]) < 1e-14) return false;
        std::swap(m[pivot], m[c]);
        std::swap(inv[pivot], inv[c]);
        const double p = m[c][c];
        for (int k = 0; k < n; ++k) {
            m[c][k] /= p;
            inv[c][k] /= p;
        }
        for (int r = 0; r < n; ++r) {
            if (r == c) continue;
            const double f = m[r][c];
            for (int k = 0; k < n; ++k) {
                m[r][k] -= f * m[c][k];
                inv[r][k] -= f * inv[c][k];
            }
        }
    }
    *out = inv;
    return true;
}

Vec polyfit(const Vec &x, const Vec &y, int degree, QString *error) {
    if (int(x.size()) <= degree) {
        if (error) *error = QObject::tr("Degree %1 needs at least %2 points").arg(degree).arg(degree + 1);
        return {};
    }
    std::vector<Vec> A(x.size(), Vec(degree + 1));
    for (size_t i = 0; i < x.size(); ++i)
        for (int j = 0; j <= degree; ++j) A[i][j] = std::pow(x[i], degree - j);
    return leastSquares(A, y, error);
}

double polyval(const Vec &p, double x) {
    double y = 0;
    for (double c : p) y = y * x + c;
    return y;
}

// --- Smoothing ------------------------------------------------------------

Vec savgolFilter(const Vec &x, int window, int polyorder, QString *error) {
    const int n = int(x.size());
    if (polyorder >= window) {
        if (error) *error = QObject::tr("polyorder must be less than window_length");
        return {};
    }
    if (window > n) {
        if (error) *error = QObject::tr("window_length must not exceed the number of samples (%1)").arg(n);
        return {};
    }
    if (window < 1) {
        if (error) *error = QObject::tr("window_length must be positive");
        return {};
    }
    const int half = window / 2;
    const double pos = window % 2 ? half : half - 0.5;
    // Weights that evaluate the local least-squares polynomial at pos:
    // c = Aᵀ (A Aᵀ)⁻¹ e₀, with positions scaled to [-1, 1] for conditioning.
    const double s = std::max(1.0, double(half));
    const int p = polyorder + 1;
    std::vector<Vec> A(p, Vec(window));
    for (int k = 0; k < p; ++k)
        for (int j = 0; j < window; ++j) A[k][j] = std::pow((j - pos) / s, k);
    Matrix G(p, Vec(p, 0.0));
    for (int a = 0; a < p; ++a)
        for (int b = 0; b < p; ++b)
            for (int j = 0; j < window; ++j) G[a][b] += A[a][j] * A[b][j];
    Matrix Gi;
    if (!inverse(G, &Gi)) {
        if (error) *error = QObject::tr("Could not compute the filter coefficients");
        return {};
    }
    Vec c(window, 0.0);
    for (int j = 0; j < window; ++j)
        for (int k = 0; k < p; ++k) c[j] += A[k][j] * Gi[k][0];

    Vec y(n, 0.0);
    // convolve1d's origin puts an even window half a sample to the right.
    const int offset = window % 2 ? half : half - 1;
    for (int i = half; i < n - half; ++i) {
        double acc = 0;
        for (int j = 0; j < window; ++j) acc += c[j] * x[i + j - offset];
        y[i] = acc;
    }
    // mode='interp': fit one polynomial to each end window instead.
    auto fitEdge = [&](int start, int from, int to) {
        Vec t(window), v(window);
        for (int j = 0; j < window; ++j) {
            t[j] = double(j);
            v[j] = x[start + j];
        }
        const double c0 = (window - 1) / 2.0, sc = std::max(1.0, c0);
        Vec ts(window);
        for (int j = 0; j < window; ++j) ts[j] = (t[j] - c0) / sc;
        const Vec coef = polyfit(ts, v, polyorder);
        for (int i = from; i < to; ++i) y[i] = polyval(coef, (i - start - c0) / sc);
    };
    fitEdge(0, 0, half);
    fitEdge(n - window, n - half, n);
    return y;
}

Vec medfilt(const Vec &x, int kernel) {
    const int n = int(x.size());
    if (kernel < 1) kernel = 1;
    if (kernel % 2 == 0) ++kernel;
    const int half = kernel / 2;
    Vec y(n), w(kernel);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < kernel; ++j) {
            const int k = i + j - half;
            w[j] = (k < 0 || k >= n) ? 0.0 : x[k];
        }
        std::nth_element(w.begin(), w.begin() + half, w.end());
        y[i] = w[half];
    }
    return y;
}

Vec detrend(const Vec &x, bool linear) {
    Vec y = x;
    const int n = int(x.size());
    if (!n) return y;
    if (!linear) {
        const double m = mean(x);
        for (double &v : y) v -= m;
        return y;
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; ++i) {
        sx += i;
        sy += x[i];
        sxx += double(i) * i;
        sxy += i * x[i];
    }
    const double d = n * sxx - sx * sx;
    const double slope = d != 0 ? (n * sxy - sx * sy) / d : 0;
    const double intercept = (sy - slope * sx) / n;
    for (int i = 0; i < n; ++i) y[i] -= slope * i + intercept;
    return y;
}

Vec decimate(const Vec &x, int q, QString *error) {
    if (q < 1) {
        if (error) *error = QObject::tr("The factor must be at least 1");
        return {};
    }
    if (q == 1) return x;
    const Sos sos = cheby1Lowpass(8, 0.05, 0.8 / q);
    const Vec y = sosfiltfilt(sos, x, error);
    if (y.empty()) return {};
    Vec out;
    for (size_t i = 0; i < y.size(); i += size_t(q)) out.push_back(y[i]);
    return out;
}

// --- Peaks ----------------------------------------------------------------

Vec peakProminences(const Vec &x, const std::vector<int> &peaks) {
    Vec prominences;
    const int n = int(x.size());
    for (int p : peaks) {
        double leftMin = x[p], rightMin = x[p];
        for (int i = p; i >= 0 && x[i] <= x[p]; --i) leftMin = std::min(leftMin, x[i]);
        for (int i = p; i < n && x[i] <= x[p]; ++i) rightMin = std::min(rightMin, x[i]);
        prominences.push_back(x[p] - std::max(leftMin, rightMin));
    }
    return prominences;
}

std::vector<int> findPeaks(const Vec &x, const PeakOptions &options) {
    const int n = int(x.size());
    std::vector<int> peaks;
    // Local maxima; a flat top counts once, at its middle.
    int i = 1;
    while (i < n - 1) {
        if (x[i - 1] < x[i]) {
            int ahead = i + 1;
            while (ahead < n - 1 && x[ahead] == x[i]) ++ahead;
            if (x[ahead] < x[i]) {
                peaks.push_back((i + ahead - 1) / 2);
                i = ahead;
            }
        }
        ++i;
    }
    if (!std::isnan(options.height)) {
        std::vector<int> kept;
        for (int p : peaks)
            if (x[p] >= options.height) kept.push_back(p);
        peaks = kept;
    }
    if (!std::isnan(options.distance) && options.distance > 1 && !peaks.empty()) {
        const int distance = int(std::ceil(options.distance));
        const int count = int(peaks.size());
        std::vector<int> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return x[peaks[a]] < x[peaks[b]]; });
        std::vector<char> keep(count, 1);
        for (int r = count - 1; r >= 0; --r) {
            const int j = order[r];
            if (!keep[j]) continue;
            for (int k = j - 1; k >= 0 && peaks[j] - peaks[k] < distance; --k) keep[k] = 0;
            for (int k = j + 1; k < count && peaks[k] - peaks[j] < distance; ++k) keep[k] = 0;
        }
        std::vector<int> kept;
        for (int k = 0; k < count; ++k)
            if (keep[k]) kept.push_back(peaks[k]);
        peaks = kept;
    }
    if (!std::isnan(options.prominence)) {
        const Vec prom = peakProminences(x, peaks);
        std::vector<int> kept;
        for (size_t k = 0; k < peaks.size(); ++k)
            if (prom[k] >= options.prominence) kept.push_back(peaks[k]);
        peaks = kept;
    }
    return peaks;
}

// --- Calculus -------------------------------------------------------------

Vec gradient(const Vec &y, const Vec &x) {
    const size_t n = y.size();
    Vec g(n, 0.0);
    if (n < 2) return g;
    g[0] = (y[1] - y[0]) / (x[1] - x[0]);
    g[n - 1] = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);
    for (size_t i = 1; i + 1 < n; ++i) {
        const double hs = x[i] - x[i - 1], hd = x[i + 1] - x[i];
        g[i] = (hs * hs * y[i + 1] + (hd * hd - hs * hs) * y[i] - hd * hd * y[i - 1]) / (hs * hd * (hd + hs));
    }
    return g;
}

double trapezoid(const Vec &y, const Vec &x) {
    double s = 0;
    for (size_t i = 1; i < y.size(); ++i) s += (x[i] - x[i - 1]) * (y[i] + y[i - 1]) / 2;
    return s;
}

Vec cumulativeTrapezoid(const Vec &y, const Vec &x) {
    Vec out;
    if (y.size() < 2) return out;
    out.reserve(y.size() - 1);
    double s = 0;
    for (size_t i = 1; i < y.size(); ++i) {
        s += (x[i] - x[i - 1]) * (y[i] + y[i - 1]) / 2;
        out.push_back(s);
    }
    return out;
}

// --- Interpolation --------------------------------------------------------

double interp(double xi, const Vec &x, const Vec &y) {
    if (x.empty()) return NAN;
    if (xi <= x.front()) return y.front();
    if (xi >= x.back()) return y.back();
    const auto it = std::upper_bound(x.begin(), x.end(), xi);
    const size_t i = size_t(it - x.begin());
    const double t = (xi - x[i - 1]) / (x[i] - x[i - 1]);
    return y[i - 1] + t * (y[i] - y[i - 1]);
}

bool CubicSpline::fit(const Vec &x, const Vec &y, QString *error) {
    const int n = int(x.size());
    if (n < 2 || y.size() != x.size()) {
        if (error) *error = QObject::tr("A spline needs at least 2 points");
        return false;
    }
    for (int i = 1; i < n; ++i)
        if (!(x[i] > x[i - 1])) {
            if (error) *error = QObject::tr("x values must be strictly increasing");
            return false;
        }
    m_x = x;
    m_y = y;
    m_m.assign(n, 0.0);
    if (n == 2) return true; // a straight line
    Vec h(n - 1);
    for (int i = 0; i < n - 1; ++i) h[i] = x[i + 1] - x[i];
    if (n == 3) {
        // Not-a-knot with three points is the parabola through them.
        const double s0 = (y[1] - y[0]) / h[0], s1 = (y[2] - y[1]) / h[1];
        const double second = 2 * (s1 - s0) / (h[0] + h[1]);
        m_m = {second, second, second};
        return true;
    }
    // Second derivatives M; not-a-knot eliminates M0 and M[n-1].
    const int k = n - 2; // unknowns M1..M[n-2]
    Vec lower(k, 0), diag(k, 0), upper(k, 0), rhs(k, 0);
    for (int r = 0; r < k; ++r) {
        const int i = r + 1;
        lower[r] = h[i - 1];
        diag[r] = 2 * (h[i - 1] + h[i]);
        upper[r] = h[i];
        rhs[r] = 6 * ((y[i + 1] - y[i]) / h[i] - (y[i] - y[i - 1]) / h[i - 1]);
    }
    // M0 = ((h0+h1) M1 - h0 M2) / h1
    diag[0] += h[0] * (h[0] + h[1]) / h[1];
    upper[0] -= h[0] * h[0] / h[1];
    // M[n-1] = ((a+b) M[n-2] - b M[n-3]) / a, a = h[n-3], b = h[n-2]
    const double a = h[n - 3], b = h[n - 2];
    diag[k - 1] += b * (a + b) / a;
    lower[k - 1] -= b * b / a;
    // Thomas algorithm.
    for (int r = 1; r < k; ++r) {
        const double f = lower[r] / diag[r - 1];
        diag[r] -= f * upper[r - 1];
        rhs[r] -= f * rhs[r - 1];
    }
    Vec M(k);
    M[k - 1] = rhs[k - 1] / diag[k - 1];
    for (int r = k - 2; r >= 0; --r) M[r] = (rhs[r] - upper[r] * M[r + 1]) / diag[r];
    for (int r = 0; r < k; ++r) m_m[r + 1] = M[r];
    m_m[0] = ((h[0] + h[1]) * m_m[1] - h[0] * m_m[2]) / h[1];
    m_m[n - 1] = ((a + b) * m_m[n - 2] - b * m_m[n - 3]) / a;
    return true;
}

double CubicSpline::operator()(double xi) const {
    const int n = int(m_x.size());
    if (n < 2) return NAN;
    // Extrapolates with the end polynomials, like CubicSpline(extrapolate=True).
    int i = int(std::upper_bound(m_x.begin(), m_x.end(), xi) - m_x.begin()) - 1;
    i = std::clamp(i, 0, n - 2);
    const double h = m_x[i + 1] - m_x[i];
    const double a = m_x[i + 1] - xi, b = xi - m_x[i];
    return m_m[i] * a * a * a / (6 * h) + m_m[i + 1] * b * b * b / (6 * h)
         + (m_y[i] / h - m_m[i] * h / 6) * a + (m_y[i + 1] / h - m_m[i + 1] * h / 6) * b;
}

// --- Regression -----------------------------------------------------------

LinearRegression linregress(const Vec &x, const Vec &y) {
    LinearRegression r;
    const size_t n = x.size();
    if (n < 2) return r;
    const double mx = mean(x), my = mean(y);
    double sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        syy += (y[i] - my) * (y[i] - my);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    r.slope = sxy / sxx;
    r.intercept = my - r.slope * mx;
    r.r = (sxx > 0 && syy > 0) ? sxy / std::sqrt(sxx * syy) : 0;
    r.r = std::clamp(r.r, -1.0, 1.0);
    if (n > 2) {
        r.slopeStderr = std::sqrt((1 - r.r * r.r) * syy / sxx / double(n - 2));
        r.interceptStderr = r.slopeStderr * std::sqrt(sxx / double(n) + mx * mx);
    }
    return r;
}

FitResult curveFit(const std::function<double(double, const Vec &)> &model, const Vec &x, const Vec &y, Vec p) {
    FitResult result;
    const int m = int(x.size()), n = int(p.size());
    if (m < n || n == 0) {
        result.error = QObject::tr("Need at least as many points as parameters");
        return result;
    }
    auto residuals = [&](const Vec &params, Vec &r) {
        double s = 0;
        for (int i = 0; i < m; ++i) {
            r[i] = y[i] - model(x[i], params);
            s += r[i] * r[i];
        }
        return s;
    };
    Vec r(m), rTrial(m);
    double cost = residuals(p, r);
    if (!std::isfinite(cost)) {
        result.error = QObject::tr("The model is not defined at the start values");
        return result;
    }
    std::vector<Vec> J(m, Vec(n));
    auto jacobian = [&]() {
        for (int j = 0; j < n; ++j) {
            Vec q = p;
            const double h = 1.49e-8 * std::max(1.0, std::abs(p[j]));
            q[j] += h;
            for (int i = 0; i < m; ++i) J[i][j] = (model(x[i], q) - model(x[i], p)) / h;
        }
    };
    double lambda = 1e-3;
    for (int iter = 0; iter < 500; ++iter) {
        jacobian();
        Matrix JtJ(n, Vec(n, 0.0));
        Vec Jtr(n, 0.0);
        for (int i = 0; i < m; ++i)
            for (int a = 0; a < n; ++a) {
                Jtr[a] += J[i][a] * r[i];
                for (int b = 0; b < n; ++b) JtJ[a][b] += J[i][a] * J[i][b];
            }
        bool improved = false;
        for (int attempt = 0; attempt < 30; ++attempt) {
            Matrix A = JtJ;
            for (int a = 0; a < n; ++a) A[a][a] += lambda * std::max(JtJ[a][a], 1e-12);
            Matrix Ai;
            if (!inverse(A, &Ai)) {
                lambda *= 10;
                continue;
            }
            Vec step(n, 0.0);
            for (int a = 0; a < n; ++a)
                for (int b = 0; b < n; ++b) step[a] += Ai[a][b] * Jtr[b];
            Vec trial = p;
            for (int a = 0; a < n; ++a) trial[a] += step[a];
            const double trialCost = residuals(trial, rTrial);
            if (std::isfinite(trialCost) && trialCost < cost) {
                const double change = (cost - trialCost) / std::max(cost, 1e-300);
                p = trial;
                r = rTrial;
                cost = trialCost;
                lambda = std::max(lambda / 10, 1e-12);
                improved = true;
                if (change < 1e-12) attempt = 1000;
                break;
            }
            lambda *= 10;
        }
        if (!improved) break;
    }
    jacobian();
    Matrix JtJ(n, Vec(n, 0.0));
    for (int i = 0; i < m; ++i)
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) JtJ[a][b] += J[i][a] * J[i][b];
    Matrix cov;
    result.stderrs.assign(n, NAN);
    if (m > n && inverse(JtJ, &cov)) {
        const double s2 = cost / (m - n);
        for (int a = 0; a < n; ++a) result.stderrs[a] = std::sqrt(std::abs(cov[a][a] * s2));
    }
    result.params = p;
    result.residual = cost;
    result.ok = true;
    return result;
}

// --- Scalar functions -----------------------------------------------------

namespace {
double brent(const std::function<double(double)> &f, double a, double b, double fa, double fb) {
    double c = a, fc = fa, d = b - a, e = d;
    for (int iter = 0; iter < 200; ++iter) {
        if ((fb > 0) == (fc > 0)) {
            c = a;
            fc = fa;
            d = e = b - a;
        }
        if (std::abs(fc) < std::abs(fb)) {
            a = b; b = c; c = a;
            fa = fb; fb = fc; fc = fa;
        }
        const double tol = 2 * 2.2e-16 * std::abs(b) + 1e-15;
        const double m = (c - b) / 2;
        if (std::abs(m) <= tol || fb == 0) return b;
        if (std::abs(e) >= tol && std::abs(fa) > std::abs(fb)) {
            double p, q, r;
            const double s = fb / fa;
            if (a == c) {
                p = 2 * m * s;
                q = 1 - s;
            } else {
                q = fa / fc;
                r = fb / fc;
                p = s * (2 * m * q * (q - r) - (b - a) * (r - 1));
                q = (q - 1) * (r - 1) * (s - 1);
            }
            if (p > 0) q = -q;
            else p = -p;
            if (2 * p < std::min(3 * m * q - std::abs(tol * q), std::abs(e * q))) {
                e = d;
                d = p / q;
            } else {
                d = m;
                e = m;
            }
        } else {
            d = m;
            e = m;
        }
        a = b;
        fa = fb;
        b += std::abs(d) > tol ? d : (m > 0 ? tol : -tol);
        fb = f(b);
    }
    return b;
}

double golden(const std::function<double(double)> &f, double a, double b, bool maximize) {
    const double r = (std::sqrt(5.0) - 1) / 2;
    auto g = [&](double x) { return maximize ? -f(x) : f(x); };
    double c = b - r * (b - a), d = a + r * (b - a);
    double fc = g(c), fd = g(d);
    for (int i = 0; i < 200 && std::abs(b - a) > 1e-12 * (1 + std::abs(a) + std::abs(b)); ++i) {
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - r * (b - a);
            fc = g(c);
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + r * (b - a);
            fd = g(d);
        }
    }
    return (a + b) / 2;
}
} // namespace

Vec roots(const std::function<double(double)> &f, double a, double b, int samples) {
    Vec found;
    if (!(b > a)) return found;
    Vec xs(samples + 1), ys(samples + 1);
    double scale = 0;
    for (int i = 0; i <= samples; ++i) {
        xs[i] = a + (b - a) * i / samples;
        ys[i] = f(xs[i]);
        if (std::isfinite(ys[i])) scale = std::max(scale, std::abs(ys[i]));
    }
    for (int i = 0; i < samples; ++i) {
        if (!std::isfinite(ys[i]) || !std::isfinite(ys[i + 1])) continue;
        if (ys[i] == 0) {
            found.push_back(xs[i]);
            continue;
        }
        if ((ys[i] < 0) != (ys[i + 1] < 0) && ys[i + 1] != 0) {
            const double root = brent(f, xs[i], xs[i + 1], ys[i], ys[i + 1]);
            // A sign change across a pole (tan, 1/x) is not a root.
            if (std::abs(f(root)) <= 1e-6 * std::max(1.0, scale)) found.push_back(root);
        }
    }
    if (ys[samples] == 0) found.push_back(xs[samples]);
    return found;
}

std::vector<Extremum> extrema(const std::function<double(double)> &f, double a, double b, int samples) {
    std::vector<Extremum> found;
    if (!(b > a)) return found;
    Vec xs(samples + 1), ys(samples + 1);
    for (int i = 0; i <= samples; ++i) {
        xs[i] = a + (b - a) * i / samples;
        ys[i] = f(xs[i]);
    }
    for (int i = 1; i < samples; ++i) {
        if (!std::isfinite(ys[i - 1]) || !std::isfinite(ys[i]) || !std::isfinite(ys[i + 1])) continue;
        const bool isMax = ys[i] > ys[i - 1] && ys[i] >= ys[i + 1];
        const bool isMin = ys[i] < ys[i - 1] && ys[i] <= ys[i + 1];
        if (!isMax && !isMin) continue;
        const double x = golden(f, xs[i - 1], xs[i + 1], isMax);
        found.push_back({x, f(x), isMax});
    }
    return found;
}

namespace {
double simpson(const std::function<double(double)> &f, double a, double b, double fa, double fm, double fb,
               double whole, double eps, int depth) {
    const double m = (a + b) / 2, lm = (a + m) / 2, rm = (m + b) / 2;
    const double flm = f(lm), frm = f(rm);
    const double left = (m - a) / 6 * (fa + 4 * flm + fm), right = (b - m) / 6 * (fm + 4 * frm + fb);
    const double delta = left + right - whole;
    if (depth <= 0 || std::abs(delta) <= 15 * eps) return left + right + delta / 15;
    return simpson(f, a, m, fa, flm, fm, left, eps / 2, depth - 1)
         + simpson(f, m, b, fm, frm, fb, right, eps / 2, depth - 1);
}
} // namespace

double integrate(const std::function<double(double)> &f, double a, double b) {
    // Split into pieces first so oscillating functions are not missed.
    const int pieces = 64;
    double total = 0;
    for (int i = 0; i < pieces; ++i) {
        const double x0 = a + (b - a) * i / pieces, x1 = a + (b - a) * (i + 1) / pieces;
        const double f0 = f(x0), f1 = f(x1), fm = f((x0 + x1) / 2);
        const double whole = (x1 - x0) / 6 * (f0 + 4 * fm + f1);
        total += simpson(f, x0, x1, f0, fm, f1, whole, 1e-12, 40);
    }
    return total;
}

int uncertaintyDecimals(double U, int significant) {
    if (!(U > 0) || !std::isfinite(U)) return 3;
    int exponent = int(std::floor(std::log10(U)));
    // Rounding can carry into the next digit (0.0996 → 0.10).
    const double rounded = std::round(U / std::pow(10.0, exponent - significant + 1)) * std::pow(10.0, exponent - significant + 1);
    exponent = int(std::floor(std::log10(rounded)));
    return significant - 1 - exponent;
}

} // namespace num
