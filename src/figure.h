#pragma once

#include "numeric.h"

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <map>

struct PlotSeries {
    enum Style { Line, Markers, LineMarkers, Stem, Bars, Arrows };
    QString label;
    QColor color;          // invalid: the next color of the cycle
    Style style = Line;
    num::Vec x, y;
    double width = 1.6;
    double markerSize = 5;
    char marker = 'o';     // 'o', 'x', '.', 's'
    bool dashed = false;
    double barWidth = 0;   // Bars: width in data units (0: from spacing)
    double opacity = 1;
};

struct PlotLine {
    enum Kind { Horizontal, Vertical, HorizontalSpan, VerticalSpan };
    Kind kind = Horizontal;
    double a = 0, b = 0;
    QColor color;
    QString label;
    bool dashed = false;
};

struct PlotBox {
    QString label;
    num::BoxStats stats;
    QColor color;
};

struct PlotAxes {
    QString title, xLabel, yLabel;
    bool logX = false, logY = false, equal = false;
    bool hasXLimits = false, hasYLimits = false;
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1;
    std::vector<PlotSeries> series;
    std::vector<PlotLine> lines;
    std::vector<PlotBox> boxes;
    QStringList categories; // x tick labels at 1, 2, ... (boxplots, bar charts)
    QString note;   // shown centered when there is nothing to draw
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
};

struct PlotPalette {
    QColor background, text, muted, grid, frame, accent;
    std::vector<QColor> cycle;
    static PlotPalette screen(const QColor &accent);
    static PlotPalette paper();
};

// A figure of one or more axes in a grid, like plt.subplots. Tools fill it;
// FigureView paints it on screen and the same painter exports it.
class Figure : public QObject {
    Q_OBJECT

public:
    explicit Figure(QObject *parent = nullptr) : QObject(parent) {}

    int rows = 1, columns = 1;
    std::vector<PlotAxes> axes;

    void set(std::vector<PlotAxes> newAxes, int rows = 1, int columns = 1);
    void clear(const QString &note = {});

    struct View { // a zoomed or panned range per axes
        double xMin, xMax, yMin, yMax;
    };
    using Views = std::map<int, View>;

    struct Layout {
        QRectF cell, plot;
        View range;
    };
    std::vector<Layout> layout(const QRectF &rect, const Views &views) const;
    void paint(QPainter *painter, const QRectF &rect, const Views &views, const PlotPalette &palette,
               qreal scale = 1) const;
    bool exportImage(const QString &path, const Views &views, QString *error) const;

signals:
    void changed();
};

class FigureView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Figure *figure READ figure WRITE setFigure NOTIFY figureChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(QString readout READ readout NOTIFY readoutChanged)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY zoomedChanged)

public:
    explicit FigureView(QQuickItem *parent = nullptr);

    Figure *figure() const { return m_figure; }
    void setFigure(Figure *figure);
    QColor accent() const { return m_accent; }
    void setAccent(const QColor &accent);
    QString readout() const { return m_readout; }
    bool zoomed() const { return !m_views.empty(); }

    void paint(QPainter *painter) override;
    Q_INVOKABLE void resetView();
    Q_INVOKABLE QString exportImage(const QString &path); // empty on success

signals:
    void figureChanged();
    void accentChanged();
    void readoutChanged();
    void zoomedChanged();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    int axesAt(const QPointF &pos, Figure::Layout *layout = nullptr) const;
    void updateReadout(const QPointF &pos);

    Figure *m_figure = nullptr;
    QColor m_accent = QColor("#FFD60A");
    Figure::Views m_views;
    QString m_readout;
    int m_dragAxes = -1;
    QPointF m_dragStart;
    Figure::View m_dragView{};
    size_t m_axesCount = 0;
};
