#include "datasource.h"

#include "expression.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>
#include <cmath>

namespace {

bool parseDouble(QStringView s, bool comma, double *out) {
    bool ok = false;
    if (comma && s.contains(',')) {
        QString t = s.toString();
        t.replace(',', '.');
        *out = t.toDouble(&ok);
    } else {
        *out = s.toDouble(&ok);
    }
    if (!ok) {
        const QString t = s.toString().toLower();
        if (t == "nan") { *out = NAN; ok = true; }
        else if (t == "inf") { *out = INFINITY; ok = true; }
        else if (t == "-inf") { *out = -INFINITY; ok = true; }
    }
    return ok;
}

QList<QStringView> splitFields(QStringView line, const QString &delimiter) {
    QList<QStringView> fields;
    if (delimiter.isEmpty()) {
        int i = 0;
        const int n = int(line.size());
        while (i < n) {
            while (i < n && line[i].isSpace()) ++i;
            const int start = i;
            while (i < n && !line[i].isSpace()) ++i;
            if (i > start) fields << line.mid(start, i - start);
        }
    } else {
        for (auto f : line.split(delimiter)) fields << f.trimmed();
        // A trailing delimiter leaves an empty last field.
        while (!fields.isEmpty() && fields.last().isEmpty()) fields.removeLast();
    }
    return fields;
}

QStringList splitHeader(const QString &line, const QString &delimiter, int columns) {
    QStringList candidates[] = {
        delimiter.isEmpty() ? QStringList() : line.split(delimiter),
        line.split('\t'),
        line.split(QRegularExpression(R"(\s{2,})")),
        line.split(QRegularExpression(R"(\s+)")),
    };
    for (auto &c : candidates) {
        for (auto &s : c) s = s.trimmed();
        c.removeAll(QString());
        if (c.size() == columns) return c;
    }
    if (columns == 1) return {line.trimmed()};
    return {};
}

} // namespace

DataTable DataTable::parseText(const QString &input, QString *error) {
    DataTable t;
    QString text = input;
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    const QStringList lines = text.split('\n');

    // The delimiter: semicolons win, then tabs or spaces, then commas when
    // they separate several numbers on every line.
    int semicolons = 0, commaLines = 0, dataLines = 0;
    for (int i = 0; i < lines.size() && dataLines < 50; ++i) {
        const QString l = lines[i].trimmed();
        if (l.isEmpty() || l.startsWith('#')) continue;
        ++dataLines;
        if (l.contains(';')) ++semicolons;
        if (l.contains(',')) ++commaLines;
    }
    if (semicolons > 0) t.delimiter = ";";
    else if (commaLines > 0 && commaLines >= dataLines - 3) {
        // "1,5 2,5" (decimal commas, space separated) vs "1.5,2.5".
        static const QRegularExpression commaNumber(R"(^\s*-?\d+,\d+(\s+-?\d+,\d+)*\s*$)");
        int decimalStyle = 0;
        for (const QString &l : lines)
            if (commaNumber.match(l).hasMatch()) ++decimalStyle;
        if (decimalStyle >= dataLines / 2) t.decimalComma = true;
        else t.delimiter = ",";
    }
    if (t.delimiter == ";") {
        // With semicolons, a comma inside a field is a decimal comma.
        for (const QString &l : lines)
            if (QRegularExpression(R"(\d,\d)").match(l).hasMatch()) {
                t.decimalComma = true;
                break;
            }
    }

    QStringList headers;     // text lines before the data, comments included
    int columns = -1;
    bool inData = false;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) {
            if (!inData) headers << line.mid(1).trimmed();
            continue;
        }
        const auto fields = splitFields(line, t.delimiter);
        std::vector<double> values;
        values.reserve(size_t(fields.size()));
        bool numeric = !fields.isEmpty();
        for (auto f : fields) {
            double v;
            if (!parseDouble(f, t.decimalComma, &v)) {
                numeric = false;
                break;
            }
            values.push_back(v);
        }
        if (!numeric) {
            if (!inData) {
                headers << line;
                ++t.skipRows;
            } else ++t.skippedLines;
            continue;
        }
        if (columns < 0) {
            columns = int(values.size());
            t.columns.assign(size_t(columns), {});
            inData = true;
        }
        if (int(values.size()) != columns) {
            ++t.skippedLines;
            continue;
        }
        for (int c = 0; c < columns; ++c) t.columns[size_t(c)].push_back(values[size_t(c)]);
    }
    if (columns <= 0) {
        if (error) *error = QObject::tr("No numbers found");
        return {};
    }

    // Column names: join the header lines that split into as many fields as
    // there are columns ("Zeit;Hoehe" + "[s];[m]" -> "Zeit [s]").
    t.names = QStringList();
    for (int c = 0; c < columns; ++c) t.names << QString();
    for (const QString &h : headers) {
        // One column: the first header line names it; later ones are notes
        // like "(Abtastrate 5120 Hz)".
        if (columns == 1 && !t.names[0].isEmpty()) break;
        const QStringList parts = splitHeader(h, t.delimiter, columns);
        if (parts.size() != columns) continue;
        for (int c = 0; c < columns; ++c)
            t.names[c] = t.names[c].isEmpty() ? parts[c] : t.names[c] + " " + parts[c];
    }
    for (int c = 0; c < columns; ++c)
        if (t.names[c].isEmpty()) t.names[c] = QObject::tr("Column %1").arg(c);

    // "(Abtastrate 5120 Hz)" or "fs = 2.56 kHz" in a header.
    static const QRegularExpression rate(R"((\d+(?:[.,]\d+)?)\s*(k?)Hz)", QRegularExpression::CaseInsensitiveOption);
    for (const QString &h : headers) {
        const auto m = rate.match(h);
        if (m.hasMatch()) {
            t.sampleRate = m.captured(1).replace(',', '.').toDouble() * (m.captured(2).isEmpty() ? 1 : 1000);
            break;
        }
    }
    return t;
}

DataTable DataTable::readWav(const QByteArray &b, QString *error) {
    DataTable t;
    auto fail = [&](const QString &m) {
        if (error) *error = m;
        return DataTable();
    };
    if (b.size() < 12 || b.mid(0, 4) != "RIFF" || b.mid(8, 4) != "WAVE") return fail(QObject::tr("Not a WAV file"));
    int pos = 12, format = 0, channels = 0, bits = 0;
    quint32 rate = 0;
    QByteArray data;
    while (pos + 8 <= b.size()) {
        const QByteArray id = b.mid(pos, 4);
        const quint32 size = qFromLittleEndian<quint32>(b.constData() + pos + 4);
        const int body = pos + 8;
        if (id == "fmt " && body + 16 <= b.size()) {
            format = qFromLittleEndian<quint16>(b.constData() + body);
            channels = qFromLittleEndian<quint16>(b.constData() + body + 2);
            rate = qFromLittleEndian<quint32>(b.constData() + body + 4);
            bits = qFromLittleEndian<quint16>(b.constData() + body + 14);
            if (format == 0xFFFE && size >= 26) format = qFromLittleEndian<quint16>(b.constData() + body + 24);
        } else if (id == "data") {
            data = b.mid(body, int(std::min<qint64>(size, b.size() - body)));
        }
        pos = body + int(size) + int(size % 2);
    }
    if (!channels || !rate || data.isEmpty()) return fail(QObject::tr("The WAV file has no audio data"));
    if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(format == 3 && (bits == 32 || bits == 64)))
        return fail(QObject::tr("Unsupported WAV format (%1-bit, format %2)").arg(bits).arg(format));
    const int bytes = bits / 8;
    const int frames = int(data.size() / (bytes * channels));
    t.columns.assign(size_t(channels), num::Vec(size_t(frames)));
    const char *d = data.constData();
    // Raw sample values, as scipy.io.wavfile.read returns them.
    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c) {
            const char *s = d + (qint64(f) * channels + c) * bytes;
            double v = 0;
            if (format == 3) v = bits == 32 ? double(qFromLittleEndian<float>(s)) : qFromLittleEndian<double>(s);
            else if (bits == 8) v = double(quint8(*s));
            else if (bits == 16) v = qFromLittleEndian<qint16>(s);
            else if (bits == 24) {
                qint32 x = (quint8(s[0])) | (quint8(s[1]) << 8) | (qint8(s[2]) << 16);
                v = x;
            } else v = qFromLittleEndian<qint32>(s);
            t.columns[size_t(c)][size_t(f)] = v;
        }
    for (int c = 0; c < channels; ++c) t.names << (channels == 1 ? QObject::tr("Audio") : QObject::tr("Channel %1").arg(c));
    t.sampleRate = rate;
    t.wav = true;
    return t;
}

DataTable DataTable::readFile(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Cannot open %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return {};
    }
    const QByteArray bytes = f.readAll();
    if (bytes.startsWith("RIFF")) return readWav(bytes, error);
    if (bytes.startsWith("PK")) {
        if (error) *error = QObject::tr("Excel files can't be read; save the sheet as CSV first");
        return {};
    }
    QString text = QString::fromUtf8(bytes);
    if (text.contains(QChar::ReplacementCharacter)) text = QString::fromLatin1(bytes);
    return parseText(text, error);
}

// --- DataSource -----------------------------------------------------------

DataSource::DataSource(bool timeBased, QObject *parent) : QObject(parent), m_timeBased(timeBased) {
    if (timeBased) {
        m_mode = "generate";
        m_xColumn = -1;
        m_formula = "3.5 + 2.5*sin(2*pi*25*t) + 1.5*sin(2*pi*37*t) + 1.25*randn()";
        m_durationText = "10";
        m_fsText = "100";
    }
    connect(this, &DataSource::inputChanged, this, &DataSource::refresh);
    refresh();
}

#define SETTER(name, member)                    \
    void DataSource::name(const QString &v) {   \
        if (v == member) return;                \
        member = v;                             \
        emit inputChanged();                    \
    }
SETTER(setMode, m_mode)
SETTER(setFsText, m_fsText)
SETTER(setScaleText, m_scaleText)
SETTER(setPasteText, m_pasteText)
SETTER(setFormula, m_formula)
SETTER(setDurationText, m_durationText)
SETTER(setSeedText, m_seedText)
#undef SETTER

void DataSource::setPath(const QString &v) {
    if (v == m_path && !m_table.columns.empty()) return;
    m_path = v;
    reloadTable();
    if (!m_loading) {
        // A new file: guess sensible columns and the sample rate.
        const int n = int(m_table.names.size());
        if (m_timeBased) {
            m_xColumn = n >= 2 ? 0 : -1;
            m_yColumn = n >= 2 ? 1 : 0;
        } else if (m_pairs) {
            m_xColumn = n >= 2 ? 0 : -2;
            m_yColumn = n >= 2 ? 1 : 0;
        } else {
            m_xColumn = -2;
            m_yColumn = 0;
        }
        m_y2Column = -1;
        if (m_table.sampleRate > 0) {
            m_fsText = pyNumber(m_table.sampleRate);
            if (m_timeBased) {
                m_xColumn = -1;
                m_yColumn = 0;
            }
        }
        m_mode = "file";
    }
    emit inputChanged();
}

QString DataSource::fileName() const { return QFileInfo(m_path).fileName(); }

void DataSource::setXColumn(int v) {
    if (v == m_xColumn) return;
    m_xColumn = v;
    emit inputChanged();
}

void DataSource::setYColumn(int v) {
    if (v == m_yColumn) return;
    m_yColumn = v;
    emit inputChanged();
}

void DataSource::setY2Column(int v) {
    if (v == m_y2Column) return;
    m_y2Column = v;
    emit inputChanged();
}

void DataSource::reloadTable() {
    m_tableError.clear();
    m_table = m_path.isEmpty() ? DataTable() : DataTable::readFile(m_path, &m_tableError);
    emit tableChanged();
}

void DataSource::refresh() {
    m_x.clear();
    m_y.clear();
    m_y2.clear();
    m_fs = NAN;
    m_error.clear();
    m_summary.clear();
    m_xLabel = m_timeBased ? tr("Time t [s]") : QString("x");
    m_yLabel = "y";
    m_y2Label.clear();
    m_active = DataTable();

    bool ok = true;
    QString message;
    const double scale = evalNumber(m_scaleText, &ok, 1.0, &message);
    if (!ok) m_error = tr("Scale: %1").arg(message);
    const double fsValue = evalNumber(m_fsText, &ok, NAN, &message);
    if (!ok && m_error.isEmpty()) m_error = tr("fs: %1").arg(message);

    if (m_mode == "generate") {
        if (!m_names.isEmpty()) {
            m_names.clear();
            emit tableChanged();
        }
        const double duration = evalNumber(m_durationText, &ok, NAN, &message);
        if (!(fsValue > 0)) m_error = tr("Enter a sampling rate fs > 0");
        else if (!(duration > 0)) m_error = tr("Enter a duration > 0");
        else if (duration * fsValue > 5e6) m_error = tr("That is more than 5 million samples");
        if (m_error.isEmpty()) {
            const Expression e = Expression::parse(m_formula, {"t"}, &message);
            if (!e.isValid()) m_error = message;
            else {
                // np.arange(0, T, 1/fs): samples strictly before T.
                const int n = int(std::ceil(duration * fsValue - 1e-9));
                const double seed = evalNumber(m_seedText, &ok, NAN);
                Expression::seedRandom(std::isnan(seed) ? 42u : unsigned(seed));
                m_x.resize(size_t(n));
                m_y.resize(size_t(n));
                for (int i = 0; i < n; ++i) {
                    m_x[size_t(i)] = i / fsValue;
                    m_y[size_t(i)] = e.eval(&m_x[size_t(i)]) * scale;
                }
                m_fs = fsValue;
                m_active.names = QStringList{"t", "y"};
                m_active.columns = {m_x, m_y};
                m_yLabel = tr("Signal y");
                m_summary = tr("%1 samples, fs = %2 Hz").arg(n).arg(formatNumber(fsValue));
            }
        }
        emit changed();
        return;
    }

    DataTable pasted;
    const DataTable *table = &m_table;
    if (m_mode == "paste") {
        if (m_pasteText.trimmed().isEmpty()) {
            m_error = tr("Paste or type numbers, one per line (two columns for x and y)");
            emit changed();
            return;
        }
        pasted = DataTable::parseText(m_pasteText, &message);
        if (pasted.columns.empty()) {
            m_error = message;
            emit changed();
            return;
        }
        table = &pasted;
    } else if (m_path.isEmpty()) {
        m_error = tr("Open a data file (Ctrl+O)");
        emit changed();
        return;
    } else if (!m_tableError.isEmpty()) {
        m_error = m_tableError;
        emit changed();
        return;
    }

    if (table->names != m_names) {
        m_names = table->names;
        emit tableChanged();
    }
    m_active = *table;
    const int cols = int(table->columns.size());
    const int rows = table->rows();
    const int yc = std::clamp(m_yColumn, 0, cols - 1);
    m_y = table->columns[size_t(yc)];
    for (double &v : m_y) v *= scale;
    m_yLabel = table->names.value(yc);
    if (m_y2Column >= 0 && m_y2Column < cols) {
        m_y2 = table->columns[size_t(m_y2Column)];
        for (double &v : m_y2) v *= scale;
        m_y2Label = table->names.value(m_y2Column);
    }
    if (m_xColumn >= 0 && m_xColumn < cols && m_xColumn != yc) {
        m_x = table->columns[size_t(m_xColumn)];
        m_xLabel = table->names.value(m_xColumn);
        if (rows > 1 && m_x[1] != m_x[0]) m_fs = 1 / (m_x[1] - m_x[0]);
    } else if (m_xColumn == -1) {
        const double fs = fsValue > 0 ? fsValue : table->sampleRate;
        if (!(fs > 0)) {
            m_error = tr("Enter the sampling rate fs (or 1/Δt) to build the time axis");
            m_x.resize(size_t(rows));
            for (int i = 0; i < rows; ++i) m_x[size_t(i)] = i;
            m_xLabel = tr("Sample index");
        } else {
            m_fs = fs;
            m_xLabel = tr("Time t [s]");
            m_x.resize(size_t(rows));
            for (int i = 0; i < rows; ++i) m_x[size_t(i)] = i / fs;
        }
    } else {
        m_x.resize(size_t(rows));
        for (int i = 0; i < rows; ++i) m_x[size_t(i)] = i;
        m_xLabel = tr("Index");
    }
    if (m_timeBased && fsValue > 0 && m_xColumn >= 0) m_fs = fsValue;
    m_summary = tr("%n rows", "", rows) + " × " + tr("%n columns", "", cols);
    if (table->skippedLines) m_summary += " · " + tr("%n lines skipped", "", table->skippedLines);
    if (table->wav) m_summary += " · " + tr("WAV, fs = %1 Hz").arg(formatNumber(table->sampleRate));
    emit changed();
}

QString DataSource::python(const QString &xName, const QString &yName, const QString &y2Name) const {
    QString code;
    const QString scale = m_scaleText.trimmed().isEmpty() ? QString() : " * " + m_scaleText.trimmed();
    if (m_mode == "generate") {
        const QString formula = Expression::parse(m_formula, {"t"}).toPython("len(t)");
        code += QString("fs = %1  # Abtastfrequenz [Hz]\n").arg(m_fsText.trimmed());
        code += QString("%1 = np.arange(0, %2, 1/fs)  # Zeitarray [s]\n").arg(xName, m_durationText.trimmed());
        code += QString("%1 = %2%3\n").arg(yName, formula.isEmpty() ? m_formula : formula, scale);
        return code;
    }
    if (m_mode == "paste") {
        code += QString("%1 = np.array([%2])%3\n")
                    .arg(yName, [&] {
                        QStringList items;
                        for (double v : m_y) items << pyNumber(scale.isEmpty() ? v : v / evalNumber(m_scaleText, nullptr, 1));
                        if (items.size() > 40) return items.mid(0, 40).join(", ") + ", ...";
                        return items.join(", ");
                    }(), scale);
        if (!y2Name.isEmpty() && !m_y2.empty()) code += QString("%1 = ...  # zweite Spalte\n").arg(y2Name);
        if (m_xColumn >= 0) code += QString("%1 = np.array([...])\n").arg(xName);
        else if (m_xColumn == -1) code += QString("fs = %1\n%2 = np.arange(len(%3)) / fs\n").arg(m_fsText.trimmed(), xName, yName);
        return code;
    }
    const QString file = fileName();
    if (m_table.wav) {
        code += QString("fs, %1 = wavfile.read('data/%2')  # from scipy.io import wavfile\n").arg(yName, file);
        if (m_table.columns.size() > 1) code += QString("%1 = %1[:, %2]\n").arg(yName).arg(m_yColumn);
        if (!scale.isEmpty()) code += QString("%1 = %1%2\n").arg(yName, scale);
        code += QString("%1 = np.arange(len(%2)) / fs\n").arg(xName, yName);
        return code;
    }
    QStringList args{QString("'data/%1'").arg(file)};
    if (!m_table.delimiter.isEmpty()) args << QString("delimiter='%1'").arg(m_table.delimiter);
    if (m_table.skipRows) args << QString("skiprows=%1").arg(m_table.skipRows);
    if (m_table.decimalComma) code += "# Achtung: Dezimalkomma - zuerst durch Punkte ersetzen\n";
    const bool single = m_table.columns.size() == 1;
    code += QString("data = np.loadtxt(%1)\n").arg(args.join(", "));
    auto column = [&](int c) { return single ? QString("data") : QString("data[:, %1]").arg(c); };
    code += QString("%1 = %2%3  # %4\n").arg(yName, column(m_yColumn), scale, m_table.names.value(m_yColumn));
    if (!y2Name.isEmpty() && m_y2Column >= 0)
        code += QString("%1 = %2%3  # %4\n").arg(y2Name, column(m_y2Column), scale, m_table.names.value(m_y2Column));
    if (m_xColumn >= 0) code += QString("%1 = %2  # %3\n").arg(xName, column(m_xColumn), m_table.names.value(m_xColumn));
    else if (m_xColumn == -1)
        code += QString("fs = %1  # Abtastfrequenz [Hz]\n%2 = np.arange(len(%3)) / fs  # Zeitarray [s]\n")
                    .arg(m_fsText.trimmed().isEmpty() ? pyNumber(m_table.sampleRate) : m_fsText.trimmed(), xName, yName);
    else code += QString("%1 = np.arange(len(%2))\n").arg(xName, yName);
    return code;
}

QString DataSource::tablePython() const {
    if (m_mode == "generate")
        return python("t", "y") + "data = np.column_stack([t, y])\n";
    if (m_mode == "paste") {
        QStringList rows;
        for (int r = 0; r < m_active.rows() && r < 30; ++r) {
            QStringList items;
            for (const auto &c : m_active.columns) items << pyNumber(c[size_t(r)]);
            rows << "[" + items.join(", ") + "]";
        }
        if (m_active.rows() > 30) rows << "...";
        return "data = np.array([" + rows.join(",\n                 ") + "])\n";
    }
    if (m_active.wav) return QString("fs, data = wavfile.read('data/%1')  # from scipy.io import wavfile\n").arg(fileName());
    QStringList args{QString("'data/%1'").arg(fileName())};
    if (!m_active.delimiter.isEmpty()) args << QString("delimiter='%1'").arg(m_active.delimiter);
    if (m_active.skipRows) args << QString("skiprows=%1").arg(m_active.skipRows);
    QString code;
    if (m_active.decimalComma) code += "# Achtung: Dezimalkomma - zuerst durch Punkte ersetzen\n";
    return code + QString("data = np.loadtxt(%1)\n").arg(args.join(", "));
}

QVariantMap DataSource::state() const {
    return {{"mode", m_mode}, {"path", m_path}, {"x", m_xColumn}, {"y", m_yColumn}, {"y2", m_y2Column},
            {"fs", m_fsText}, {"scale", m_scaleText}, {"paste", m_pasteText}, {"formula", m_formula},
            {"duration", m_durationText}, {"seed", m_seedText}};
}

void DataSource::setState(const QVariantMap &s) {
    if (s.isEmpty()) return;
    m_loading = true;
    m_mode = s.value("mode", m_mode).toString();
    m_path = s.value("path").toString();
    m_xColumn = s.value("x", m_xColumn).toInt();
    m_yColumn = s.value("y", m_yColumn).toInt();
    m_y2Column = s.value("y2", m_y2Column).toInt();
    m_fsText = s.value("fs", m_fsText).toString();
    m_scaleText = s.value("scale").toString();
    m_pasteText = s.value("paste").toString();
    m_formula = s.value("formula", m_formula).toString();
    m_durationText = s.value("duration", m_durationText).toString();
    m_seedText = s.value("seed").toString();
    reloadTable();
    m_loading = false;
    emit inputChanged();
}
