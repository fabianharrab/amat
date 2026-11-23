#include "figure.h"

#include "expression.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QBuffer>
#include <QWheelEvent>
#include <climits>
#include <cmath>

PlotPalette PlotPalette::screen(const QColor &accent) {
    PlotPalette p;
    p.background = QColor("#141417");
    p.text = QColor("#e6e6e6");
    p.muted = QColor("#8a8a93");
    p.grid = QColor(255, 255, 255, 22);
    p.frame = QColor(255, 255, 255, 60);
    p.accent = accent;
    p.cycle = {accent, QColor("#5ac8fa"), QColor("#ff6b6b"), QColor("#7bd88f"), QColor("#c792ea"),
               QColor("#ffb86c"), QColor("#f78fb3"), QColor("#a0a0a8")};
    return p;
}

// Exported images look like Matplotlib's defaults, ready for a report.
PlotPalette PlotPalette::paper() {
    PlotPalette p;
    p.background = Qt::white;
    p.text = QColor("#111111");
    p.muted = QColor("#555555");
    p.grid = QColor(0, 0, 0, 30);
    p.frame = QColor(0, 0, 0, 160);
    p.accent = QColor("#1f77b4");
    p.cycle = {QColor("#1f77b4"), QColor("#ff7f0e"), QColor("#2ca02c"), QColor("#d62728"), QColor("#9467bd"),
               QColor("#8c564b"), QColor("#e377c2"), QColor("#7f7f7f")};
    return p;
}

void Figure::set(std::vector<PlotAxes> newAxes, int r, int c) {
    axes = std::move(newAxes);
    rows = std::max(1, r);
    columns = std::max(1, c);
    emit changed();
}

void Figure::clear(const QString &note) {
    PlotAxes a;
    a.note = note;
    set({a});
}

namespace {

double fwd(double v, bool log) { return log ? (v > 0 ? std::log10(v) : NAN) : v; }
double inv(double v, bool log) { return log ? std::pow(10.0, v) : v; }

struct Range {
    double lo = INFINITY, hi = -INFINITY;
    void add(double v) {
        if (!std::isfinite(v)) return;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    bool valid() const { return lo <= hi; }
};

Figure::View autoRange(const PlotAxes &a) {
    Range x, y;
    for (const auto &s : a.series) {
        const size_t n = std::min(s.x.size(), s.y.size());
        for (size_t i = 0; i < n; ++i) {
            const double fx = fwd(s.x[i], a.logX), fy = fwd(s.y[i], a.logY);
            if (!std::isfinite(fx) || !std::isfinite(fy)) continue;
            x.add(fx);
            y.add(fy);
        }
        if (s.style == PlotSeries::Bars || s.style == PlotSeries::Stem) {
            if (!a.logY) y.add(0);
            if (s.style == PlotSeries::Bars && n > 0) {
                double w = s.barWidth;
                if (w <= 0) w = n > 1 ? std::abs(s.x[1] - s.x[0]) : 1;
                x.add(fwd(s.x.front() - w / 2, a.logX));
                x.add(fwd(s.x[n - 1] + w / 2, a.logX));
            }
        }
    }
    for (const auto &l : a.lines) {
        if (l.kind == PlotLine::Horizontal) y.add(fwd(l.a, a.logY));
        if (l.kind == PlotLine::HorizontalSpan) {
            y.add(fwd(l.a, a.logY));
            y.add(fwd(l.b, a.logY));
        }
        if (l.kind == PlotLine::Vertical) x.add(fwd(l.a, a.logX));
    }
    for (size_t i = 0; i < a.boxes.size(); ++i) {
        const auto &b = a.boxes[i].stats;
        y.add(b.whiskerLow);
        y.add(b.whiskerHigh);
        for (double o : b.outliers) y.add(o);
    }
    const size_t categories = std::max(a.boxes.size(), size_t(a.categories.size()));
    if (categories) {
        x.add(0.5);
        x.add(double(categories) + 0.5);
    }
    Figure::View v{0, 1, 0, 1};
    if (x.valid()) {
        v.xMin = x.lo;
        v.xMax = x.hi;
    }
    if (y.valid()) {
        v.yMin = y.lo;
        v.yMax = y.hi;
    }
    auto pad = [](double &lo, double &hi, double fraction) {
        if (hi - lo <= 0) {
            const double d = lo == 0 ? 1 : std::abs(lo) * 0.1;
            lo -= d;
            hi += d;
            return;
        }
        const double p = (hi - lo) * fraction;
        lo -= p;
        hi += p;
    };
    if (!categories) pad(v.xMin, v.xMax, 0.02);
    pad(v.yMin, v.yMax, 0.06);
    if (a.hasXLimits) {
        v.xMin = fwd(a.xMin, a.logX);
        v.xMax = fwd(a.xMax, a.logX);
    }
    if (a.hasYLimits) {
        v.yMin = fwd(a.yMin, a.logY);
        v.yMax = fwd(a.yMax, a.logY);
    }
    return v;
}

// Tick positions at 1, 2, or 5 times a power of ten.
std::vector<double> niceTicks(double lo, double hi, int target) {
    std::vector<double> ticks;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) return ticks;
    const double raw = (hi - lo) / std::max(2, target);
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    double step = magnitude;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * magnitude >= raw) {
            step = m * magnitude;
            break;
        }
    for (double t = std::ceil(lo / step) * step; t <= hi + step * 1e-9; t += step)
        ticks.push_back(std::abs(t) < step * 1e-9 ? 0.0 : t);
    return ticks;
}

QString tickLabel(double v, bool log) {
    if (log) {
        const double e = std::round(v);
        if (std::abs(e) <= 3) return formatNumber(std::pow(10.0, e), 4);
        return QString("1e%1").arg(int(e));
    }
    return formatNumber(v, 6);
}

} // namespace

std::vector<Figure::Layout> Figure::layout(const QRectF &rect, const Views &views) const {
    std::vector<Layout> out;
    const double cw = rect.width() / columns, ch = rect.height() / rows;
    for (size_t i = 0; i < axes.size(); ++i) {
        const PlotAxes &a = axes[i];
        Layout l;
        l.cell = QRectF(rect.left() + a.column * cw, rect.top() + a.row * ch, cw * a.columnSpan, ch * a.rowSpan);
        const double top = a.title.isEmpty() ? 14 : 30;
        const double bottom = a.xLabel.isEmpty() ? 30 : 46;
        const double left = a.yLabel.isEmpty() ? 58 : 76;
        l.plot = l.cell.adjusted(left, top, -16, -bottom);
        auto it = views.find(int(i));
        l.range = it != views.end() ? it->second : autoRange(a);
        if (a.equal && it == views.end() && l.plot.width() > 0 && l.plot.height() > 0) {
            // Same scale on both axes, like ax.axis('equal').
            const double sx = (l.range.xMax - l.range.xMin) / l.plot.width();
            const double sy = (l.range.yMax - l.range.yMin) / l.plot.height();
            if (sx > sy) {
                const double c = (l.range.yMin + l.range.yMax) / 2, h = sx * l.plot.height() / 2;
                l.range.yMin = c - h;
                l.range.yMax = c + h;
            } else {
                const double c = (l.range.xMin + l.range.xMax) / 2, w = sy * l.plot.width() / 2;
                l.range.xMin = c - w;
                l.range.xMax = c + w;
            }
        }
        out.push_back(l);
    }
    return out;
}

void Figure::paint(QPainter *p, const QRectF &rect, const Views &views, const PlotPalette &pal, qreal scale) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->fillRect(rect, pal.background);
    QFont base = QGuiApplication::font();
    base.setPixelSize(int(std::round(12 * scale)));
    QFont small = base;
    small.setPixelSize(int(std::round(11 * scale)));
    QFont bold = base;
    bold.setPixelSize(int(std::round(13 * scale)));
    bold.setWeight(QFont::DemiBold);

    const auto layouts = layout(QRectF(rect.topLeft() / scale, rect.size() / scale), views);
    for (size_t ai = 0; ai < axes.size(); ++ai) {
        const PlotAxes &a = axes[ai];
        const Layout &L = layouts[ai];
        const QRectF plot(L.plot.topLeft() * scale, L.plot.size() * scale);
        const QRectF cell(L.cell.topLeft() * scale, L.cell.size() * scale);
        const View v = L.range;
        if (plot.width() < 20 || plot.height() < 20) continue;

        auto px = [&](double x) { return plot.left() + (fwd(x, a.logX) - v.xMin) / (v.xMax - v.xMin) * plot.width(); };
        auto py = [&](double y) { return plot.bottom() - (fwd(y, a.logY) - v.yMin) / (v.yMax - v.yMin) * plot.height(); };

        const bool empty = a.series.empty() && a.boxes.empty() && a.lines.empty();
        if (empty && !a.note.isEmpty()) {
            p->setPen(pal.muted);
            p->setFont(base);
            p->drawText(cell, Qt::AlignCenter | Qt::TextWordWrap, a.note);
            continue;
        }

        // Grid and tick labels.
        p->setFont(small);
        const QFontMetricsF fm(small);
        auto ticksFor = [&](double lo, double hi, bool log, int target) {
            if (!log) return niceTicks(lo, hi, target);
            std::vector<double> t;
            for (double e = std::ceil(lo); e <= hi; e += std::max(1.0, std::ceil((hi - lo) / target))) t.push_back(e);
            if (t.size() < 2) return niceTicks(lo, hi, target);
            return t;
        };
        const auto xt = ticksFor(v.xMin, v.xMax, a.logX, int(plot.width() / (90 * scale)));
        const auto yt = ticksFor(v.yMin, v.yMax, a.logY, int(plot.height() / (46 * scale)));
        p->setPen(QPen(pal.grid, 1 * scale));
        for (double t : xt) {
            const double x = plot.left() + (t - v.xMin) / (v.xMax - v.xMin) * plot.width();
            p->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (double t : yt) {
            const double y = plot.bottom() - (t - v.yMin) / (v.yMax - v.yMin) * plot.height();
            p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        p->setPen(pal.muted);
        for (double t : xt) {
            const double x = plot.left() + (t - v.xMin) / (v.xMax - v.xMin) * plot.width();
            const QString s = a.categories.isEmpty() ? tickLabel(t, a.logX) : QString();
            p->drawText(QRectF(x - 60 * scale, plot.bottom() + 4 * scale, 120 * scale, fm.height()), Qt::AlignHCenter, s);
        }
        for (double t : yt) {
            const double y = plot.bottom() - (t - v.yMin) / (v.yMax - v.yMin) * plot.height();
            p->drawText(QRectF(cell.left(), y - fm.height() / 2, plot.left() - cell.left() - 6 * scale, fm.height()),
                        Qt::AlignRight | Qt::AlignVCenter, tickLabel(t, a.logY));
        }
        for (int b = 0; b < a.categories.size(); ++b) {
            const double x = px(double(b) + 1);
            const double slot = std::abs(px(double(b) + 1.5) - px(double(b) + 0.5)) - 4 * scale;
            p->drawText(QRectF(x - slot / 2, plot.bottom() + 4 * scale, slot, fm.height()), Qt::AlignHCenter,
                        fm.elidedText(a.categories[b], Qt::ElideRight, slot));
        }

        // Labels and title.
        p->setPen(pal.text);
        p->setFont(base);
        if (!a.xLabel.isEmpty())
            p->drawText(QRectF(plot.left(), plot.bottom() + 20 * scale, plot.width(), 22 * scale), Qt::AlignCenter, a.xLabel);
        if (!a.yLabel.isEmpty()) {
            p->save();
            p->translate(cell.left() + 12 * scale, plot.center().y());
            p->rotate(-90);
            p->drawText(QRectF(-plot.height() / 2, -10 * scale, plot.height(), 22 * scale), Qt::AlignCenter, a.yLabel);
            p->restore();
        }
        if (!a.title.isEmpty()) {
            p->setFont(bold);
            p->drawText(QRectF(plot.left(), cell.top() + 4 * scale, plot.width(), 22 * scale), Qt::AlignCenter, a.title);
        }

        p->save();
        p->setClipRect(plot.adjusted(-1, -1, 1, 1));
        int colorIndex = 0;
        auto nextColor = [&](const QColor &c) {
            if (c.isValid()) return c;
            return pal.cycle[size_t(colorIndex++) % pal.cycle.size()];
        };

        // Spans and reference lines go underneath the data.
        std::vector<std::pair<QString, QColor>> legend;
        std::vector<int> legendStyle; // 0 line, 1 marker, 2 patch
        for (const auto &l : a.lines) {
            const QColor c = nextColor(l.color);
            if (l.kind == PlotLine::HorizontalSpan || l.kind == PlotLine::VerticalSpan) {
                QColor fill = c;
                fill.setAlphaF(0.14f);
                const QRectF r = l.kind == PlotLine::HorizontalSpan
                                     ? QRectF(QPointF(plot.left(), py(l.b)), QPointF(plot.right(), py(l.a)))
                                     : QRectF(QPointF(px(l.a), plot.top()), QPointF(px(l.b), plot.bottom()));
                p->fillRect(r.normalized(), fill);
                if (!l.label.isEmpty()) {
                    legend.push_back({l.label, fill});
                    legendStyle.push_back(2);
                }
                continue;
            }
            QPen pen(c, 1.4 * scale);
            if (l.dashed) pen.setStyle(Qt::DashLine);
            p->setPen(pen);
            if (l.kind == PlotLine::Horizontal) p->drawLine(QPointF(plot.left(), py(l.a)), QPointF(plot.right(), py(l.a)));
            else p->drawLine(QPointF(px(l.a), plot.top()), QPointF(px(l.a), plot.bottom()));
            if (!l.label.isEmpty()) {
                legend.push_back({l.label, c});
                legendStyle.push_back(0);
            }
        }

        for (const auto &s : a.series) {
            const QColor c0 = nextColor(s.color);
            QColor c = c0;
            c.setAlphaF(float(s.opacity));
            const size_t n = std::min(s.x.size(), s.y.size());
            if (!s.label.isEmpty()) {
                legend.push_back({s.label, c});
                legendStyle.push_back(s.style == PlotSeries::Markers ? 1 : s.style == PlotSeries::Bars ? 2 : 0);
            }
            if (n == 0) continue;
            QPen pen(c, s.width * scale, s.dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

            if (s.style == PlotSeries::Bars) {
                double w = s.barWidth;
                if (w <= 0) w = n > 1 ? std::abs(s.x[1] - s.x[0]) : 1;
                QColor fill = c;
                fill.setAlphaF(0.55f * float(s.opacity));
                p->setPen(QPen(c, 1 * scale));
                p->setBrush(fill);
                for (size_t i = 0; i < n; ++i) {
                    const double base = a.logY ? (v.yMin > -300 ? inv(v.yMin, true) : 1e-300) : 0;
                    const QRectF r(QPointF(px(s.x[i] - w / 2), py(s.y[i])), QPointF(px(s.x[i] + w / 2), py(base)));
                    p->drawRect(r.normalized());
                }
                p->setBrush(Qt::NoBrush);
                continue;
            }
            if (s.style == PlotSeries::Arrows) {
                // Consecutive pairs of points are vectors from start to tip.
                p->setPen(pen);
                p->setBrush(c);
                for (size_t i = 0; i + 1 < n; i += 2) {
                    const QPointF a0(px(s.x[i]), py(s.y[i])), a1(px(s.x[i + 1]), py(s.y[i + 1]));
                    p->drawLine(a0, a1);
                    const double len = std::hypot(a1.x() - a0.x(), a1.y() - a0.y());
                    if (len < 2) continue;
                    const double ux = (a1.x() - a0.x()) / len, uy = (a1.y() - a0.y()) / len;
                    const double head = std::min(12.0 * scale, len / 2);
                    QPolygonF tip;
                    tip << a1 << QPointF(a1.x() - head * ux + head * 0.45 * uy, a1.y() - head * uy - head * 0.45 * ux)
                        << QPointF(a1.x() - head * ux - head * 0.45 * uy, a1.y() - head * uy + head * 0.45 * ux);
                    p->drawPolygon(tip);
                }
                p->setBrush(Qt::NoBrush);
                continue;
            }
            if (s.style == PlotSeries::Stem) {
                p->setPen(QPen(c, 1.2 * scale));
                for (size_t i = 0; i < n; ++i) p->drawLine(QPointF(px(s.x[i]), py(a.logY ? inv(v.yMin, true) : 0)), QPointF(px(s.x[i]), py(s.y[i])));
            }
            if (s.style == PlotSeries::Line || s.style == PlotSeries::LineMarkers) {
                p->setPen(pen);
                // Long signals: one vertical min–max stroke per pixel column,
                // which looks identical and stays fast at 300k samples.
                bool sorted = true;
                for (size_t i = 1; i < n && sorted; ++i) sorted = s.x[i] >= s.x[i - 1];
                const double columns = plot.width();
                if (sorted && double(n) > 3 * columns) {
                    QPainterPath path;
                    bool started = false;
                    int column = -1;
                    double lo = 0, hi = 0, first = 0, last = 0;
                    auto flush = [&]() {
                        if (column < 0) return;
                        const double x = plot.left() + column + 0.5;
                        if (!started) {
                            path.moveTo(x, first);
                            started = true;
                        } else path.lineTo(x, first);
                        path.lineTo(x, lo);
                        path.lineTo(x, hi);
                        path.lineTo(x, last);
                    };
                    for (size_t i = 0; i < n; ++i) {
                        const double X = px(s.x[i]), Y = py(s.y[i]);
                        if (!std::isfinite(X) || !std::isfinite(Y)) continue;
                        if (X < plot.left() - 2 || X > plot.right() + 2) continue;
                        const int col = int(std::floor(X - plot.left()));
                        if (col != column) {
                            flush();
                            column = col;
                            lo = hi = first = last = Y;
                        } else {
                            lo = std::min(lo, Y);
                            hi = std::max(hi, Y);
                            last = Y;
                        }
                    }
                    flush();
                    p->drawPath(path);
                } else {
                    QPainterPath path;
                    bool pen_down = false;
                    for (size_t i = 0; i < n; ++i) {
                        const double X = px(s.x[i]), Y = py(s.y[i]);
                        if (!std::isfinite(X) || !std::isfinite(Y) || std::abs(Y) > 1e7 || std::abs(X) > 1e7) {
                            pen_down = false;
                            continue;
                        }
                        if (pen_down) path.lineTo(X, Y);
                        else path.moveTo(X, Y);
                        pen_down = true;
                    }
                    p->drawPath(path);
                }
            }
            if (s.style == PlotSeries::Markers || s.style == PlotSeries::LineMarkers || s.style == PlotSeries::Stem) {
                const double r = s.markerSize * scale / 2;
                p->setPen(QPen(c, std::max(1.0, 1.4 * scale)));
                p->setBrush(s.marker == 'x' ? Qt::NoBrush : QBrush(c));
                const bool many = n > 20000;
                for (size_t i = 0; i < n; ++i) {
                    const double X = px(s.x[i]), Y = py(s.y[i]);
                    if (!std::isfinite(X) || !std::isfinite(Y) || !plot.adjusted(-r, -r, r, r).contains(X, Y)) continue;
                    if (many || s.marker == '.') p->drawPoint(QPointF(X, Y));
                    else if (s.marker == 'x') {
                        p->drawLine(QPointF(X - r, Y - r), QPointF(X + r, Y + r));
                        p->drawLine(QPointF(X - r, Y + r), QPointF(X + r, Y - r));
                    } else if (s.marker == 's') p->drawRect(QRectF(X - r, Y - r, 2 * r, 2 * r));
                    else p->drawEllipse(QPointF(X, Y), r, r);
                }
                p->setBrush(Qt::NoBrush);
            }
        }

        // Boxplots at x = 1, 2, ... like plt.boxplot.
        for (size_t b = 0; b < a.boxes.size(); ++b) {
            const auto &box = a.boxes[b];
            const QColor c = nextColor(box.color);
            const double x = double(b) + 1, w = 0.25;
            QColor fill = c;
            fill.setAlphaF(0.25f);
            p->setPen(QPen(c, 1.5 * scale));
            p->setBrush(fill);
            p->drawRect(QRectF(QPointF(px(x - w), py(box.stats.q3)), QPointF(px(x + w), py(box.stats.q1))).normalized());
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(pal.text, 2 * scale));
            p->drawLine(QPointF(px(x - w), py(box.stats.median)), QPointF(px(x + w), py(box.stats.median)));
            p->setPen(QPen(c, 1.5 * scale));
            p->drawLine(QPointF(px(x), py(box.stats.q3)), QPointF(px(x), py(box.stats.whiskerHigh)));
            p->drawLine(QPointF(px(x), py(box.stats.q1)), QPointF(px(x), py(box.stats.whiskerLow)));
            p->drawLine(QPointF(px(x - w / 2), py(box.stats.whiskerHigh)), QPointF(px(x + w / 2), py(box.stats.whiskerHigh)));
            p->drawLine(QPointF(px(x - w / 2), py(box.stats.whiskerLow)), QPointF(px(x + w / 2), py(box.stats.whiskerLow)));
            for (double o : box.stats.outliers) p->drawEllipse(QPointF(px(x), py(o)), 3 * scale, 3 * scale);
        }
        p->restore();

        p->setPen(QPen(pal.frame, 1 * scale));
        p->drawRect(plot);

        // Legend in the top right corner.
        if (!legend.empty()) {
            p->setFont(small);
            double width = 0;
            for (const auto &e : legend) width = std::max(width, fm.horizontalAdvance(e.first));
            const double row = fm.height() + 4 * scale;
            const QSizeF size(width + 36 * scale, row * double(legend.size()) + 8 * scale);
            const double m = 8 * scale;
            // Like loc='best': the corner that covers the fewest data points.
            const QRectF corners[] = {
                QRectF(QPointF(plot.right() - size.width() - m, plot.top() + m), size),
                QRectF(QPointF(plot.left() + m, plot.top() + m), size),
                QRectF(QPointF(plot.right() - size.width() - m, plot.bottom() - size.height() - m), size),
                QRectF(QPointF(plot.left() + m, plot.bottom() - size.height() - m), size),
            };
            QRectF box = corners[0];
            int fewest = INT_MAX;
            for (const QRectF &c : corners) {
                int hits = 0;
                for (const auto &s : a.series) {
                    const size_t n = std::min(s.x.size(), s.y.size());
                    const size_t step = std::max<size_t>(1, n / 1500);
                    for (size_t i = 0; i < n; i += step)
                        if (c.contains(px(s.x[i]), py(s.y[i]))) ++hits;
                }
                if (hits < fewest) {
                    fewest = hits;
                    box = c;
                }
            }
            QColor bg = pal.background;
            bg.setAlphaF(0.85f);
            p->setPen(QPen(pal.frame, 1 * scale));
            p->setBrush(bg);
            p->drawRect(box);
            p->setBrush(Qt::NoBrush);
            for (size_t i = 0; i < legend.size(); ++i) {
                const double y = box.top() + 4 * scale + row * double(i) + row / 2;
                const QColor c = legend[i].second;
                if (legendStyle[i] == 1) {
                    p->setPen(Qt::NoPen);
                    p->setBrush(c);
                    p->drawEllipse(QPointF(box.left() + 15 * scale, y), 3.5 * scale, 3.5 * scale);
                    p->setBrush(Qt::NoBrush);
                } else if (legendStyle[i] == 2) {
                    p->fillRect(QRectF(box.left() + 8 * scale, y - 5 * scale, 14 * scale, 10 * scale), c);
                } else {
                    p->setPen(QPen(c, 2 * scale));
                    p->drawLine(QPointF(box.left() + 6 * scale, y), QPointF(box.left() + 24 * scale, y));
                }
                p->setPen(pal.text);
                p->drawText(QRectF(box.left() + 30 * scale, y - row / 2, width + 4 * scale, row), Qt::AlignVCenter, legend[i].first);
            }
        }
    }
    p->restore();
}

bool Figure::exportImage(const QString &path, const Views &views, QString *error) const {
    const qreal scale = 2;
    const QSize logical(columns > 1 ? 1200 : 1000, rows > 1 ? 380 * rows : 520);
    if (QFileInfo(path).suffix().toLower() == "svg") {
        // Vector output, as plt.savefig('….svg').
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QSvgGenerator svg;
        svg.setOutputDevice(&buffer);
        svg.setSize(logical);
        svg.setViewBox(QRect(QPoint(0, 0), logical));
        svg.setTitle(QFileInfo(path).completeBaseName());
        QPainter painter(&svg);
        paint(&painter, QRectF(QPointF(0, 0), QSizeF(logical)), views, PlotPalette::paper(), 1);
        painter.end();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(buffer.data()) < 0 || !file.commit()) {
            if (error) *error = file.errorString();
            return false;
        }
        return true;
    }
    QImage image(logical * scale, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    paint(&painter, QRectF(QPointF(0, 0), QSizeF(logical) * scale), views, PlotPalette::paper(), scale);
    painter.end();
    // Written beside the target and renamed into place.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (!image.save(&file, suffix == "jpg" || suffix == "jpeg" ? "JPG" : "PNG")) {
        if (error) *error = tr("Could not encode the image");
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

// --- FigureView -----------------------------------------------------------

FigureView::FigureView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
}

void FigureView::setFigure(Figure *figure) {
    if (figure == m_figure) return;
    if (m_figure) disconnect(m_figure, nullptr, this, nullptr);
    m_figure = figure;
    if (m_figure)
        connect(m_figure, &Figure::changed, this, [this] {
            // Keep a zoom across recalculations while the layout is the same.
            if (m_figure->axes.size() != m_axesCount) {
                m_views.clear();
                emit zoomedChanged();
            }
            m_axesCount = m_figure->axes.size();
            update();
        });
    m_axesCount = m_figure ? m_figure->axes.size() : 0;
    m_views.clear();
    emit figureChanged();
    update();
}

void FigureView::setAccent(const QColor &accent) {
    if (accent == m_accent) return;
    m_accent = accent;
    emit accentChanged();
    update();
}

void FigureView::paint(QPainter *painter) {
    if (!m_figure) return;
    m_figure->paint(painter, QRectF(0, 0, width(), height()), m_views, PlotPalette::screen(m_accent));
}

void FigureView::resetView() {
    m_views.clear();
    emit zoomedChanged();
    update();
}

QString FigureView::exportImage(const QString &path) {
    if (!m_figure) return tr("Nothing to export");
    QString error;
    return m_figure->exportImage(path, m_views, &error) ? QString() : error;
}

int FigureView::axesAt(const QPointF &pos, Figure::Layout *out) const {
    if (!m_figure) return -1;
    const auto layouts = m_figure->layout(QRectF(0, 0, width(), height()), m_views);
    for (size_t i = 0; i < layouts.size(); ++i)
        if (layouts[i].plot.contains(pos)) {
            if (out) *out = layouts[i];
            return int(i);
        }
    return -1;
}

void FigureView::wheelEvent(QWheelEvent *event) {
    Figure::Layout l;
    const int i = axesAt(event->position(), &l);
    if (i < 0) {
        event->ignore();
        return;
    }
    const double factor = std::pow(0.85, event->angleDelta().y() / 120.0);
    const double fx = (event->position().x() - l.plot.left()) / l.plot.width();
    const double fy = (l.plot.bottom() - event->position().y()) / l.plot.height();
    Figure::View v = l.range;
    const double cx = v.xMin + fx * (v.xMax - v.xMin), cy = v.yMin + fy * (v.yMax - v.yMin);
    // Ctrl zooms only the x axis, Shift only y; plain wheel zooms both.
    const auto mods = event->modifiers();
    if (!(mods & Qt::ShiftModifier)) {
        v.xMin = cx - (cx - v.xMin) * factor;
        v.xMax = cx + (v.xMax - cx) * factor;
    }
    if (!(mods & Qt::ControlModifier)) {
        v.yMin = cy - (cy - v.yMin) * factor;
        v.yMax = cy + (v.yMax - cy) * factor;
    }
    m_views[i] = v;
    emit zoomedChanged();
    update();
    updateReadout(event->position());
}

void FigureView::mousePressEvent(QMouseEvent *event) {
    Figure::Layout l;
    m_dragAxes = axesAt(event->position(), &l);
    if (m_dragAxes < 0) {
        event->ignore();
        return;
    }
    m_dragStart = event->position();
    m_dragView = l.range;
}

void FigureView::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragAxes < 0) return;
    Figure::Layout l;
    if (axesAt(m_dragStart, &l) != m_dragAxes) return;
    const QPointF d = event->position() - m_dragStart;
    Figure::View v = m_dragView;
    const double dx = d.x() / l.plot.width() * (v.xMax - v.xMin);
    const double dy = d.y() / l.plot.height() * (v.yMax - v.yMin);
    v.xMin -= dx;
    v.xMax -= dx;
    v.yMin += dy;
    v.yMax += dy;
    m_views[m_dragAxes] = v;
    emit zoomedChanged();
    update();
    updateReadout(event->position());
}

void FigureView::mouseReleaseEvent(QMouseEvent *) { m_dragAxes = -1; }

void FigureView::mouseDoubleClickEvent(QMouseEvent *) { resetView(); }

void FigureView::hoverMoveEvent(QHoverEvent *event) { updateReadout(event->position()); }

void FigureView::hoverLeaveEvent(QHoverEvent *) {
    m_readout.clear();
    emit readoutChanged();
}

void FigureView::updateReadout(const QPointF &pos) {
    Figure::Layout l;
    const int i = axesAt(pos, &l);
    QString text;
    if (i >= 0) {
        const PlotAxes &a = m_figure->axes[size_t(i)];
        const double fx = (pos.x() - l.plot.left()) / l.plot.width();
        const double fy = (l.plot.bottom() - pos.y()) / l.plot.height();
        const double x = inv(l.range.xMin + fx * (l.range.xMax - l.range.xMin), a.logX);
        const double y = inv(l.range.yMin + fy * (l.range.yMax - l.range.yMin), a.logY);
        text = QString("x = %1   y = %2").arg(formatNumber(x, 5), formatNumber(y, 5));
    }
    if (text != m_readout) {
        m_readout = text;
        emit readoutChanged();
    }
}
