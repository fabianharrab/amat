#pragma once

#include "numeric.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>

// A table of numbers read from a text file, pasted text, or a WAV file.
struct DataTable {
    QStringList names;
    std::vector<num::Vec> columns;
    int skipRows = 0;        // non-comment header lines, for np.loadtxt(skiprows=)
    QString delimiter;       // for np.loadtxt(delimiter=); empty means whitespace
    bool decimalComma = false;
    double sampleRate = 0;   // from a WAV header or a "5120 Hz" note in the header
    bool wav = false;
    int skippedLines = 0;

    int rows() const { return columns.empty() ? 0 : int(columns[0].size()); }
    static DataTable parseText(const QString &text, QString *error);
    static DataTable readFile(const QString &path, QString *error);
    static DataTable readWav(const QByteArray &bytes, QString *error);
};

// Where a page gets its numbers: a file, pasted values, or a formula in t.
// Pages bind its properties to their controls and read x() and y().
class DataSource : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY inputChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY inputChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY inputChanged)
    Q_PROPERTY(QStringList columnNames READ columnNames NOTIFY tableChanged)
    Q_PROPERTY(int xColumn READ xColumn WRITE setXColumn NOTIFY inputChanged)
    Q_PROPERTY(int yColumn READ yColumn WRITE setYColumn NOTIFY inputChanged)
    Q_PROPERTY(int y2Column READ y2Column WRITE setY2Column NOTIFY inputChanged)
    Q_PROPERTY(QString fsText READ fsText WRITE setFsText NOTIFY inputChanged)
    Q_PROPERTY(QString scaleText READ scaleText WRITE setScaleText NOTIFY inputChanged)
    Q_PROPERTY(QString pasteText READ pasteText WRITE setPasteText NOTIFY inputChanged)
    Q_PROPERTY(QString formula READ formula WRITE setFormula NOTIFY inputChanged)
    Q_PROPERTY(QString durationText READ durationText WRITE setDurationText NOTIFY inputChanged)
    Q_PROPERTY(QString seedText READ seedText WRITE setSeedText NOTIFY inputChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool timeBased READ timeBased CONSTANT)

public:
    // `timeBased` pages (signal, spectrum) default x to time from fs.
    explicit DataSource(bool timeBased, QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    void setMode(const QString &v);
    QString path() const { return m_path; }
    void setPath(const QString &v);
    QString fileName() const;
    QStringList columnNames() const { return m_names; }
    int xColumn() const { return m_xColumn; }
    void setXColumn(int v);
    int yColumn() const { return m_yColumn; }
    void setYColumn(int v);
    int y2Column() const { return m_y2Column; }
    void setY2Column(int v);
    QString fsText() const { return m_fsText; }
    void setFsText(const QString &v);
    QString scaleText() const { return m_scaleText; }
    void setScaleText(const QString &v);
    QString pasteText() const { return m_pasteText; }
    void setPasteText(const QString &v);
    QString formula() const { return m_formula; }
    void setFormula(const QString &v);
    QString durationText() const { return m_durationText; }
    void setDurationText(const QString &v);
    QString seedText() const { return m_seedText; }
    void setSeedText(const QString &v);
    bool timeBased() const { return m_timeBased; }
    // x/y pairs (fit): a new two-column file maps to x = column 0, y = column 1.
    void setPairs(bool pairs) { m_pairs = pairs; }

    QString summary() const { return m_summary; }
    QString error() const { return m_error; }

    // Results of the last refresh().
    const num::Vec &x() const { return m_x; }
    const num::Vec &y() const { return m_y; }
    const num::Vec &y2() const { return m_y2; }
    double fs() const { return m_fs; }      // NaN when unknown
    QString xLabel() const { return m_xLabel; }
    QString yLabel() const { return m_yLabel; }
    QString y2Label() const { return m_y2Label; }
    // Python that loads the data into `xName`/`yName` (and y2Name).
    QString python(const QString &xName, const QString &yName, const QString &y2Name = {}) const;

    // The whole table in use (file, pasted, or t and y from a formula).
    const DataTable &table() const { return m_active; }
    // Python that loads the whole table into `data`.
    QString tablePython() const;

    QVariantMap state() const;
    void setState(const QVariantMap &state);

signals:
    void inputChanged();
    void tableChanged();
    void changed();

private:
    void reloadTable();
    void refresh();

    bool m_timeBased;
    bool m_pairs = false;
    QString m_mode = "paste";
    QString m_path;
    int m_xColumn = -2, m_yColumn = 0, m_y2Column = -1;
    QString m_fsText, m_scaleText, m_pasteText, m_formula, m_durationText, m_seedText;
    DataTable m_table;
    QString m_tableError;
    num::Vec m_x, m_y, m_y2;
    double m_fs = NAN;
    QString m_xLabel, m_yLabel, m_y2Label, m_summary, m_error;
    QStringList m_names; // columns of the file or pasted table in use
    DataTable m_active;
    bool m_loading = false;
};
