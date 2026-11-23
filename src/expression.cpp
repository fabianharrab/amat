#include "expression.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>
#include <random>

namespace {

enum class Kind { Number, Constant, Variable, Negate, Add, Subtract, Multiply, Divide, Power, Modulo, Call };

struct Function {
    const char *name;
    int arity;
    const char *python; // printed name; "%1" placeholders for arguments
};

// Every function a formula may call. Several spellings map to one entry so
// both `asin` and NumPy's `arcsin` work.
const Function kFunctions[] = {
    {"sin", 1, "np.sin(%1)"},        {"cos", 1, "np.cos(%1)"},         {"tan", 1, "np.tan(%1)"},
    {"arcsin", 1, "np.arcsin(%1)"},  {"arccos", 1, "np.arccos(%1)"},   {"arctan", 1, "np.arctan(%1)"},
    {"sinh", 1, "np.sinh(%1)"},      {"cosh", 1, "np.cosh(%1)"},       {"tanh", 1, "np.tanh(%1)"},
    {"exp", 1, "np.exp(%1)"},        {"log", 1, "np.log(%1)"},         {"log10", 1, "np.log10(%1)"},
    {"log2", 1, "np.log2(%1)"},      {"sqrt", 1, "np.sqrt(%1)"},       {"cbrt", 1, "np.cbrt(%1)"},
    {"abs", 1, "np.abs(%1)"},        {"sign", 1, "np.sign(%1)"},       {"floor", 1, "np.floor(%1)"},
    {"ceil", 1, "np.ceil(%1)"},      {"round", 1, "np.round(%1)"},     {"deg2rad", 1, "np.deg2rad(%1)"},
    {"rad2deg", 1, "np.rad2deg(%1)"}, {"square", 1, "np.square(%1)"},  {"arctan2", 2, "np.arctan2(%1, %2)"},
    {"hypot", 2, "np.hypot(%1, %2)"}, {"minimum", 2, "np.minimum(%1, %2)"}, {"maximum", 2, "np.maximum(%1, %2)"},
    {"power", 2, "np.power(%1, %2)"}, {"rand", 0, "np.random.rand(%n)"}, {"randn", 0, "np.random.randn(%n)"},
    {"squarewave", 1, "signal.square(%1)"}, {"sawtooth", 1, "signal.sawtooth(%1)"},
    {"triangle", 1, "signal.sawtooth(%1, 0.5)"}, {"step", 1, "np.heaviside(%1, 1)"},
};
constexpr int kFunctionCount = int(sizeof(kFunctions) / sizeof(kFunctions[0]));

const QHash<QString, QString> &aliases() {
    static const QHash<QString, QString> map{
        {"asin", "arcsin"}, {"acos", "arccos"}, {"atan", "arctan"}, {"atan2", "arctan2"},
        {"ln", "log"}, {"radians", "deg2rad"}, {"degrees", "rad2deg"}, {"absolute", "abs"},
        {"min", "minimum"}, {"max", "maximum"}, {"pow", "power"}, {"heaviside", "step"},
    };
    return map;
}

int functionIndex(QString name) {
    name = aliases().value(name, name);
    for (int i = 0; i < kFunctionCount; ++i)
        if (name == QLatin1String(kFunctions[i].name))
            return i;
    return -1;
}

struct Constant {
    const char *name;
    double value;
    const char *python;
};

// The constants the course uses, with scipy.constants' values.
const Constant kConstants[] = {
    {"pi", M_PI, "np.pi"}, {"e", M_E, "np.e"}, {"g", 9.80665, "constants.g"},
    {"c", 299792458.0, "constants.c"}, {"mach", 340.5, "constants.mach"},
    {"inch", 0.0254, "constants.inch"}, {"inf", INFINITY, "np.inf"},
};

int constantIndex(const QString &name) {
    for (int i = 0; i < int(sizeof(kConstants) / sizeof(kConstants[0])); ++i)
        if (name == QLatin1String(kConstants[i].name))
            return i;
    return -1;
}

std::mt19937_64 &engine() {
    static std::mt19937_64 e(42);
    return e;
}

double squareWave(double x) {
    // scipy.signal.square: +1 for the first half of each 2π period.
    double phase = std::fmod(x, 2 * M_PI);
    if (phase < 0) phase += 2 * M_PI;
    return phase < M_PI ? 1.0 : -1.0;
}

double sawtoothWave(double x, double width = 1.0) {
    double phase = std::fmod(x, 2 * M_PI);
    if (phase < 0) phase += 2 * M_PI;
    const double r = phase / (2 * M_PI);
    if (r < width)
        return -1 + 2 * r / width;
    return 1 - 2 * (r - width) / (1 - width);
}

} // namespace

struct Expression::Node {
    Kind kind = Kind::Number;
    double value = 0;
    int index = -1; // variable, constant, or function index
    std::vector<NodePtr> args;
};

using NodePtr = Expression::NodePtr;
using Node = Expression::Node;

namespace {

NodePtr number(double v) {
    auto n = std::make_shared<Node>();
    n->kind = Kind::Number;
    n->value = v;
    return n;
}

NodePtr unary(Kind kind, NodePtr a) {
    auto n = std::make_shared<Node>();
    n->kind = kind;
    n->args = {std::move(a)};
    return n;
}

NodePtr call(int function, std::vector<NodePtr> args) {
    auto n = std::make_shared<Node>();
    n->kind = Kind::Call;
    n->index = function;
    n->args = std::move(args);
    return n;
}

NodePtr call(const char *name, NodePtr a) { return call(functionIndex(name), {std::move(a)}); }

bool isNumber(const NodePtr &n, double v) { return n->kind == Kind::Number && n->value == v; }

NodePtr binary(Kind kind, NodePtr a, NodePtr b);

// The number at the far left of a product chain: the 4 of 4*pi**2*L.
bool leadingNumber(const NodePtr &n, double *value) {
    if (n->kind != Kind::Multiply) return false;
    if (n->args[0]->kind == Kind::Number) {
        *value = n->args[0]->value;
        return true;
    }
    return leadingNumber(n->args[0], value);
}

NodePtr withoutLeading(const NodePtr &n) {
    if (n->args[0]->kind == Kind::Number) return n->args[1];
    return binary(Kind::Multiply, withoutLeading(n->args[0]), n->args[1]);
}

// Builds a binary node, folding constants and dropping identities so that
// symbolic derivatives print the way someone would write them by hand.
NodePtr binary(Kind kind, NodePtr a, NodePtr b) {
    if (a->kind == Kind::Number && b->kind == Kind::Number) {
        const double x = a->value, y = b->value;
        switch (kind) {
        case Kind::Add: return number(x + y);
        case Kind::Subtract: return number(x - y);
        case Kind::Multiply: return number(x * y);
        case Kind::Divide: if (y != 0 && std::fmod(x, y) == 0) return number(x / y); break;
        case Kind::Power: if (y == std::round(y) && std::abs(y) < 6) return number(std::pow(x, y)); break;
        default: break;
        }
    }
    switch (kind) {
    case Kind::Add:
        if (isNumber(a, 0)) return b;
        if (isNumber(b, 0)) return a;
        if (b->kind == Kind::Negate) return binary(Kind::Subtract, a, b->args[0]);
        break;
    case Kind::Subtract:
        if (isNumber(b, 0)) return a;
        if (isNumber(a, 0)) return b->kind == Kind::Negate ? b->args[0] : unary(Kind::Negate, b);
        if (b->kind == Kind::Negate) return binary(Kind::Add, a, b->args[0]);
        break;
    case Kind::Multiply:
        if (isNumber(a, 0) || isNumber(b, 0)) return number(0);
        if (isNumber(a, 1)) return b;
        if (isNumber(b, 1)) return a;
        if (isNumber(a, -1)) return b->kind == Kind::Negate ? b->args[0] : unary(Kind::Negate, b);
        if (a->kind == Kind::Negate) {
            auto inner = binary(Kind::Multiply, a->args[0], b);
            return inner->kind == Kind::Negate ? inner->args[0] : unary(Kind::Negate, inner);
        }
        if (b->kind == Kind::Negate) {
            auto inner = binary(Kind::Multiply, a, b->args[0]);
            return inner->kind == Kind::Negate ? inner->args[0] : unary(Kind::Negate, inner);
        }
        // Keep numbers in front and merged: 2*(4*x) -> 8*x, x*(2*y) -> 2*x*y.
        if (b->kind == Kind::Number && a->kind != Kind::Number) return binary(Kind::Multiply, b, a);
        if (double c; leadingNumber(b, &c)) {
            if (a->kind == Kind::Number) return binary(Kind::Multiply, number(a->value * c), withoutLeading(b));
            return binary(Kind::Multiply, number(c), binary(Kind::Multiply, a, withoutLeading(b)));
        }
        if (double c; a->kind != Kind::Number && leadingNumber(a, &c))
            return binary(Kind::Multiply, number(c), binary(Kind::Multiply, withoutLeading(a), b));
        break;
    case Kind::Divide:
        if (isNumber(a, 0)) return number(0);
        if (isNumber(b, 1)) return a;
        if (a->kind == Kind::Negate) {
            auto inner = binary(Kind::Divide, a->args[0], b);
            return inner->kind == Kind::Negate ? inner->args[0] : unary(Kind::Negate, inner);
        }
        break;
    case Kind::Power:
        if (isNumber(b, 0)) return number(1);
        if (isNumber(b, 1)) return a;
        break;
    default:
        break;
    }
    auto n = std::make_shared<Node>();
    n->kind = kind;
    n->args = {std::move(a), std::move(b)};
    return n;
}

NodePtr negate(NodePtr a) {
    if (a->kind == Kind::Number) return number(-a->value);
    if (a->kind == Kind::Negate) return a->args[0];
    return unary(Kind::Negate, std::move(a));
}

NodePtr add(NodePtr a, NodePtr b) { return binary(Kind::Add, std::move(a), std::move(b)); }
NodePtr sub(NodePtr a, NodePtr b) { return binary(Kind::Subtract, std::move(a), std::move(b)); }
NodePtr mul(NodePtr a, NodePtr b) { return binary(Kind::Multiply, std::move(a), std::move(b)); }
NodePtr div(NodePtr a, NodePtr b) { return binary(Kind::Divide, std::move(a), std::move(b)); }
NodePtr pow(NodePtr a, NodePtr b) { return binary(Kind::Power, std::move(a), std::move(b)); }

NodePtr constant(const char *name) {
    auto n = std::make_shared<Node>();
    n->kind = Kind::Constant;
    n->index = constantIndex(name);
    n->value = kConstants[n->index].value;
    return n;
}

class Parser {
public:
    Parser(const QString &text, const QStringList &variables) : m_variables(variables) {
        // NumPy, math, and scipy prefixes are accepted and ignored, so code
        // pasted from a notebook evaluates as is.
        static const QRegularExpression prefixes(R"(\b(?:np|numpy|math|scipy|constants|signal)\.)");
        m_text = QString(text).replace(prefixes, QString());
    }

    NodePtr parse(QString *error) {
        NodePtr root;
        try {
            root = expression();
            skipSpace();
            if (m_pos < m_text.size())
                fail(QObject::tr("Unexpected \"%1\"").arg(m_text.mid(m_pos, 8)));
        } catch (const QString &message) {
            if (error) *error = message;
            return {};
        }
        return root;
    }

private:
    [[noreturn]] void fail(const QString &message) { throw message; }

    void skipSpace() {
        while (m_pos < m_text.size() && m_text[m_pos].isSpace()) ++m_pos;
    }

    bool accept(const QString &token) {
        skipSpace();
        if (m_text.mid(m_pos, token.size()) == token) {
            m_pos += token.size();
            return true;
        }
        return false;
    }

    NodePtr expression() {
        NodePtr left = term();
        for (;;) {
            if (accept("+")) left = binaryRaw(Kind::Add, left, term());
            else if (accept("-")) left = binaryRaw(Kind::Subtract, left, term());
            else return left;
        }
    }

    NodePtr term() {
        NodePtr left = unaryTerm();
        for (;;) {
            skipSpace();
            if (m_text.mid(m_pos, 2) == "**") return left;
            if (accept("*") || accept("·") || accept("×")) left = binaryRaw(Kind::Multiply, left, unaryTerm());
            else if (accept("/")) left = binaryRaw(Kind::Divide, left, unaryTerm());
            else if (accept("%")) left = binaryRaw(Kind::Modulo, left, unaryTerm());
            else if (startsImplicitFactor()) left = binaryRaw(Kind::Multiply, left, unaryTerm());
            else return left;
        }
    }

    // "2pi" or "3(x+1)": a number directly followed by a name or bracket.
    bool startsImplicitFactor() {
        skipSpace();
        if (m_pos >= m_text.size() || !m_lastWasNumber) return false;
        const QChar c = m_text[m_pos];
        return c.isLetter() || c == '(' || c == QChar(0x03C0);
    }

    NodePtr unaryTerm() {
        if (accept("-") || accept("−")) return unary(Kind::Negate, unaryTerm());
        if (accept("+")) return unaryTerm();
        return power();
    }

    NodePtr power() {
        NodePtr base = primary();
        skipSpace();
        if (accept("**") || accept("^")) {
            // Right-associative and binding tighter than unary minus on the
            // left, like Python: -2**2 == -4, 2**-1 == 0.5.
            return binaryRaw(Kind::Power, base, unaryTerm());
        }
        if (accept("²")) return binaryRaw(Kind::Power, base, number(2));
        if (accept("³")) return binaryRaw(Kind::Power, base, number(3));
        return base;
    }

    NodePtr primary() {
        skipSpace();
        m_lastWasNumber = false;
        if (m_pos >= m_text.size()) fail(QObject::tr("Unexpected end of formula"));
        const QChar c = m_text[m_pos];
        if (c == '(') {
            ++m_pos;
            NodePtr inner = expression();
            if (!accept(")")) fail(QObject::tr("Missing \")\""));
            return inner;
        }
        if (c.isDigit() || c == '.') return numberLiteral();
        if (c == QChar(0x03C0)) { // π
            ++m_pos;
            return constant("pi");
        }
        if (c.isLetter() || c == '_') return name();
        fail(QObject::tr("Unexpected \"%1\"").arg(c));
    }

    NodePtr numberLiteral() {
        static const QRegularExpression pattern(R"((\d+\.?\d*|\.\d+)([eE][+-]?\d+)?)");
        const auto match = pattern.match(m_text, m_pos, QRegularExpression::NormalMatch,
                                         QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch()) fail(QObject::tr("Bad number"));
        m_pos += match.capturedLength();
        m_lastWasNumber = true;
        auto n = number(match.captured().toDouble());
        m_lastWasNumber = true;
        return n;
    }

    NodePtr name() {
        const int start = m_pos;
        while (m_pos < m_text.size() && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == '_')) ++m_pos;
        const QString id = m_text.mid(start, m_pos - start);
        skipSpace();
        const int variable = m_variables.indexOf(id);
        if (m_pos < m_text.size() && m_text[m_pos] == '(' && variable < 0) {
            const int fn = functionIndex(id);
            if (fn < 0) fail(QObject::tr("Unknown function \"%1\"").arg(id));
            ++m_pos;
            std::vector<NodePtr> args;
            if (!accept(")")) {
                do args.push_back(expression());
                while (accept(","));
                if (!accept(")")) fail(QObject::tr("Missing \")\""));
            }
            // sawtooth(x, 0.5) is the triangle wave; other extra arguments
            // (like a size for randn) are dropped.
            int index = fn;
            if (QLatin1String(kFunctions[fn].name) == QLatin1String("sawtooth") && args.size() == 2
                && args[1]->kind == Kind::Number && args[1]->value == 0.5)
                index = functionIndex("triangle");
            const int arity = kFunctions[index].arity;
            if (int(args.size()) < arity)
                fail(QObject::tr("%1() needs %2 arguments").arg(id).arg(arity));
            args.resize(arity);
            return call(index, std::move(args));
        }
        if (variable >= 0) {
            auto n = std::make_shared<Node>();
            n->kind = Kind::Variable;
            n->index = variable;
            return n;
        }
        if (constantIndex(id) >= 0) return constant(id.toLatin1().constData());
        fail(QObject::tr("Unknown name \"%1\"").arg(id));
    }

    // While parsing, keep the tree exactly as written; simplification is
    // only for derivatives.
    static NodePtr binaryRaw(Kind kind, NodePtr a, NodePtr b) {
        auto n = std::make_shared<Node>();
        n->kind = kind;
        n->args = {std::move(a), std::move(b)};
        return n;
    }

    QString m_text;
    QStringList m_variables;
    int m_pos = 0;
    bool m_lastWasNumber = false;
};

double evalNode(const Node &n, const double *values) {
    switch (n.kind) {
    case Kind::Number:
    case Kind::Constant: return n.value;
    case Kind::Variable: return values[n.index];
    case Kind::Negate: return -evalNode(*n.args[0], values);
    case Kind::Add: return evalNode(*n.args[0], values) + evalNode(*n.args[1], values);
    case Kind::Subtract: return evalNode(*n.args[0], values) - evalNode(*n.args[1], values);
    case Kind::Multiply: return evalNode(*n.args[0], values) * evalNode(*n.args[1], values);
    case Kind::Divide: return evalNode(*n.args[0], values) / evalNode(*n.args[1], values);
    case Kind::Power: return std::pow(evalNode(*n.args[0], values), evalNode(*n.args[1], values));
    case Kind::Modulo: {
        const double a = evalNode(*n.args[0], values), b = evalNode(*n.args[1], values);
        const double r = std::fmod(a, b);
        return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r; // Python's sign rule
    }
    case Kind::Call: break;
    }
    const double a = n.args.size() > 0 ? evalNode(*n.args[0], values) : 0;
    const double b = n.args.size() > 1 ? evalNode(*n.args[1], values) : 0;
    const QLatin1String name(kFunctions[n.index].name);
    static thread_local std::normal_distribution<double> normal(0, 1);
    static thread_local std::uniform_real_distribution<double> uniform(0, 1);
    if (name == QLatin1String("sin")) return std::sin(a);
    if (name == QLatin1String("cos")) return std::cos(a);
    if (name == QLatin1String("tan")) return std::tan(a);
    if (name == QLatin1String("arcsin")) return std::asin(a);
    if (name == QLatin1String("arccos")) return std::acos(a);
    if (name == QLatin1String("arctan")) return std::atan(a);
    if (name == QLatin1String("sinh")) return std::sinh(a);
    if (name == QLatin1String("cosh")) return std::cosh(a);
    if (name == QLatin1String("tanh")) return std::tanh(a);
    if (name == QLatin1String("exp")) return std::exp(a);
    if (name == QLatin1String("log")) return std::log(a);
    if (name == QLatin1String("log10")) return std::log10(a);
    if (name == QLatin1String("log2")) return std::log2(a);
    if (name == QLatin1String("sqrt")) return std::sqrt(a);
    if (name == QLatin1String("cbrt")) return std::cbrt(a);
    if (name == QLatin1String("abs")) return std::abs(a);
    if (name == QLatin1String("sign")) return a > 0 ? 1 : a < 0 ? -1 : 0;
    if (name == QLatin1String("floor")) return std::floor(a);
    if (name == QLatin1String("ceil")) return std::ceil(a);
    if (name == QLatin1String("round")) return std::nearbyint(a); // half to even, like NumPy
    if (name == QLatin1String("deg2rad")) return a * M_PI / 180;
    if (name == QLatin1String("rad2deg")) return a * 180 / M_PI;
    if (name == QLatin1String("square")) return a * a;
    if (name == QLatin1String("arctan2")) return std::atan2(a, b);
    if (name == QLatin1String("hypot")) return std::hypot(a, b);
    if (name == QLatin1String("minimum")) return std::min(a, b);
    if (name == QLatin1String("maximum")) return std::max(a, b);
    if (name == QLatin1String("power")) return std::pow(a, b);
    if (name == QLatin1String("rand")) return uniform(engine());
    if (name == QLatin1String("randn")) return normal(engine());
    if (name == QLatin1String("squarewave")) return squareWave(a);
    if (name == QLatin1String("sawtooth")) return sawtoothWave(a);
    if (name == QLatin1String("triangle")) return sawtoothWave(a, 0.5);
    if (name == QLatin1String("step")) return a >= 0 ? 1 : 0;
    return NAN;
}

bool depends(const Node &n, int variable) {
    if (n.kind == Kind::Variable) return n.index == variable;
    for (const auto &a : n.args)
        if (depends(*a, variable)) return true;
    return false;
}

// Symbolic derivative; returns null when a function has none.
NodePtr differentiate(const NodePtr &n, int v) {
    if (!depends(*n, v)) return number(0);
    switch (n->kind) {
    case Kind::Number:
    case Kind::Constant: return number(0);
    case Kind::Variable: return number(1);
    default: break;
    }
    const NodePtr u = n->args.empty() ? NodePtr() : n->args[0];
    const NodePtr w = n->args.size() > 1 ? n->args[1] : NodePtr();
    NodePtr du = u ? differentiate(u, v) : NodePtr();
    NodePtr dw = w ? differentiate(w, v) : NodePtr();
    if ((u && !du) || (w && !dw)) return {};
    switch (n->kind) {
    case Kind::Negate: return negate(du);
    case Kind::Add: return add(du, dw);
    case Kind::Subtract: return sub(du, dw);
    case Kind::Multiply: return add(mul(du, w), mul(u, dw));
    case Kind::Divide:
        if (!depends(*w, v)) return div(du, w);
        // u / b**n with constant u: -n*u*b' / b**(n+1), as written by hand.
        if (!depends(*u, v) && w->kind == Kind::Power && w->args[1]->kind == Kind::Number) {
            const double e = w->args[1]->value;
            const NodePtr db = differentiate(w->args[0], v);
            if (!db) return {};
            return negate(div(mul(mul(number(e), u), db), pow(w->args[0], number(e + 1))));
        }
        if (!depends(*u, v)) return negate(div(mul(u, dw), pow(w, number(2))));
        return div(sub(mul(du, w), mul(u, dw)), pow(w, number(2)));
    case Kind::Power:
        if (!depends(*w, v)) {
            NodePtr exponent = w->kind == Kind::Number ? number(w->value - 1) : sub(w, number(1));
            return mul(mul(w, pow(u, exponent)), du);
        }
        if (!depends(*u, v)) return mul(mul(n, call("log", u)), dw);
        return mul(n, add(mul(dw, call("log", u)), div(mul(w, du), u)));
    case Kind::Modulo: return du;
    default: break;
    }
    const QLatin1String name(kFunctions[n->index].name);
    auto chain = [&](NodePtr outer) { return mul(outer, du); };
    if (name == QLatin1String("sin")) return chain(call("cos", u));
    if (name == QLatin1String("cos")) return chain(negate(call("sin", u)));
    if (name == QLatin1String("tan")) return div(du, pow(call("cos", u), number(2)));
    if (name == QLatin1String("arcsin")) return div(du, call("sqrt", sub(number(1), pow(u, number(2)))));
    if (name == QLatin1String("arccos")) return negate(div(du, call("sqrt", sub(number(1), pow(u, number(2))))));
    if (name == QLatin1String("arctan")) return div(du, add(number(1), pow(u, number(2))));
    if (name == QLatin1String("sinh")) return chain(call("cosh", u));
    if (name == QLatin1String("cosh")) return chain(call("sinh", u));
    if (name == QLatin1String("tanh")) return div(du, pow(call("cosh", u), number(2)));
    if (name == QLatin1String("exp")) return chain(n);
    if (name == QLatin1String("log")) return div(du, u);
    if (name == QLatin1String("log10")) return div(du, mul(u, call("log", number(10))));
    if (name == QLatin1String("log2")) return div(du, mul(u, call("log", number(2))));
    if (name == QLatin1String("sqrt")) return div(du, mul(number(2), n));
    if (name == QLatin1String("cbrt")) return div(du, mul(number(3), pow(n, number(2))));
    if (name == QLatin1String("abs")) return chain(call("sign", u));
    if (name == QLatin1String("deg2rad")) return mul(div(constant("pi"), number(180)), du);
    if (name == QLatin1String("rad2deg")) return mul(div(number(180), constant("pi")), du);
    if (name == QLatin1String("square")) return mul(mul(number(2), u), du);
    if (name == QLatin1String("arctan2")) // atan2(y, x): (x y' - y x') / (x² + y²)
        return div(sub(mul(w, du), mul(u, dw)), add(pow(w, number(2)), pow(u, number(2))));
    if (name == QLatin1String("hypot")) return div(add(mul(u, du), mul(w, dw)), n);
    if (name == QLatin1String("power")) {
        auto asPower = std::make_shared<Node>();
        asPower->kind = Kind::Power;
        asPower->args = {u, w};
        return differentiate(asPower, v);
    }
    if (name == QLatin1String("sign") || name == QLatin1String("step") || name == QLatin1String("floor")
        || name == QLatin1String("ceil") || name == QLatin1String("round"))
        return number(0);
    return {};
}

int precedence(const Node &n) {
    switch (n.kind) {
    case Kind::Add:
    case Kind::Subtract: return 1;
    case Kind::Multiply:
    case Kind::Divide:
    case Kind::Modulo: return 2;
    case Kind::Negate: return 3;
    case Kind::Power: return 4;
    case Kind::Number: return n.value < 0 ? 3 : 5;
    default: return 5;
    }
}

QString print(const Node &n, const QStringList &variables, const QString &count) {
    auto wrap = [&](const Node &child, int needed) {
        const QString s = print(child, variables, count);
        return precedence(child) < needed ? "(" + s + ")" : s;
    };
    switch (n.kind) {
    case Kind::Number: return pyNumber(n.value);
    case Kind::Constant: return kConstants[n.index].python;
    case Kind::Variable: return variables.value(n.index);
    // -a*b/c and a*(b*c) need no brackets: both read the same in Python.
    case Kind::Negate: return "-" + wrap(*n.args[0], 2);
    case Kind::Add: return wrap(*n.args[0], 1) + " + " + wrap(*n.args[1], 2);
    case Kind::Subtract: return wrap(*n.args[0], 1) + " - " + wrap(*n.args[1], 2);
    case Kind::Multiply:
        return wrap(*n.args[0], 2) + "*" + wrap(*n.args[1], n.args[1]->kind == Kind::Multiply ? 2 : 3);
    case Kind::Divide: return wrap(*n.args[0], 2) + "/" + wrap(*n.args[1], 3);
    case Kind::Modulo: return wrap(*n.args[0], 2) + " % " + wrap(*n.args[1], 3);
    case Kind::Power: return wrap(*n.args[0], 5) + "**" + wrap(*n.args[1], 4);
    case Kind::Call: break;
    }
    QString text = kFunctions[n.index].python;
    for (int i = 0; i < int(n.args.size()); ++i)
        text.replace(QString("%%1").arg(i + 1), print(*n.args[i], variables, count));
    return text.replace("%n", count);
}

} // namespace

Expression Expression::parse(const QString &text, const QStringList &variables, QString *error) {
    Expression e;
    if (text.trimmed().isEmpty()) {
        if (error) *error = QObject::tr("Empty formula");
        return e;
    }
    Parser parser(text, variables);
    e.m_root = parser.parse(error);
    e.m_variables = variables;
    return e;
}

QStringList Expression::freeNames(const QString &text) {
    static const QRegularExpression prefixes(R"(\b(?:np|numpy|math|scipy|constants|signal)\.)");
    static const QRegularExpression identifier(R"(([A-Za-z_][A-Za-z0-9_]*)\s*(\()?)");
    static const QRegularExpression exponent(R"(\d[eE][+-]?\d)");
    QString cleaned = QString(text).replace(prefixes, QString());
    QStringList names;
    auto it = identifier.globalMatch(cleaned);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString id = m.captured(1);
        const int start = m.capturedStart(1);
        // Skip the "e5" of "1e5" and the "x" of "2x" counts as a name.
        if (start > 0 && (cleaned[start - 1].isDigit() || cleaned[start - 1] == '.')
            && (id[0] == 'e' || id[0] == 'E') && exponent.match(cleaned, start - 1).capturedStart() == start - 1)
            continue;
        if (!m.captured(2).isEmpty() && functionIndex(id) >= 0) continue;
        if (constantIndex(id) >= 0) continue;
        if (!names.contains(id)) names << id;
    }
    return names;
}

double Expression::eval(const double *values) const {
    return m_root ? evalNode(*m_root, values) : NAN;
}

Expression Expression::derivative(int variable) const {
    Expression d;
    if (m_root) d.m_root = differentiate(m_root, variable);
    d.m_variables = m_variables;
    return d;
}

bool Expression::dependsOn(int variable) const { return m_root && depends(*m_root, variable); }

QString Expression::toPython(const QString &sampleCount) const {
    return m_root ? print(*m_root, m_variables, sampleCount) : QString();
}

QStringList Expression::functionNames() {
    QStringList names;
    for (const auto &f : kFunctions) names << f.name;
    return names;
}

void Expression::seedRandom(unsigned seed) { engine().seed(seed); }

double evalNumber(const QString &text, bool *ok, double fallback, QString *error) {
    if (ok) *ok = true;
    QString t = text.trimmed();
    if (t.isEmpty()) return fallback;
    // A decimal comma ("0,05") is common in German-speaking input.
    static const QRegularExpression decimalComma(R"(^[-+]?\d+,\d+$)");
    if (decimalComma.match(t).hasMatch()) t.replace(',', '.');
    QString message;
    const Expression e = Expression::parse(t, {}, &message);
    if (!e.isValid()) {
        if (ok) *ok = false;
        if (error) *error = message;
        return fallback;
    }
    return e.eval();
}

std::vector<double> evalList(const QString &text, QString *error) {
    std::vector<double> values;
    QString t = text;
    t.remove('[').remove(']');
    // With semicolons or newlines as separators, commas are decimal commas.
    const bool commaDecimal = (t.contains(';') || t.contains('\n') || t.contains('\t'))
                              && QRegularExpression(R"(\d,\d)").match(t).hasMatch()
                              && !QRegularExpression(R"(\d,\s)").match(t).hasMatch();
    if (commaDecimal) t.replace(',', '.');
    static const QRegularExpression separators(R"([;,\s]+)");
    for (const QString &item : t.split(separators, Qt::SkipEmptyParts)) {
        bool ok = false;
        QString message;
        const double v = evalNumber(item, &ok, 0, &message);
        if (!ok) {
            if (error) *error = QObject::tr("\"%1\": %2").arg(item, message);
            return {};
        }
        values.push_back(v);
    }
    return values;
}

QString formatNumber(double value, int digits) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "∞" : "−∞";
    if (value == 0) return "0";
    const double magnitude = std::abs(value);
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    if (magnitude >= 1e7 || magnitude < 1e-4) {
        QString s = locale.toString(value, 'e', digits - 1);
        // Trim trailing zeros in the mantissa: 1.500000e+03 -> 1.5e+03.
        const int e = int(s.indexOf(QRegularExpression("[eE]")));
        const QString point = locale.decimalPoint();
        if (e > 0 && s.left(e).contains(point)) {
            QString mantissa = s.left(e);
            while (mantissa.endsWith('0')) mantissa.chop(1);
            if (mantissa.endsWith(point)) mantissa.chop(point.size());
            s = mantissa + s.mid(e);
        }
        return s;
    }
    return locale.toString(value, 'g', digits);
}

QString pyNumber(double value) {
    if (std::isinf(value)) return value > 0 ? "np.inf" : "-np.inf";
    if (std::isnan(value)) return "np.nan";
    if (value == std::round(value) && std::abs(value) < 1e15) return QString::number(qint64(value));
    // Shortest representation within 15 significant digits, so rounding
    // noise like 0.30000000000000004 prints as 0.3.
    for (int p = 1; p <= 15; ++p) {
        const QString candidate = QString::number(value, 'g', p);
        if (std::abs(candidate.toDouble() - value) <= 1e-15 * std::abs(value)) return candidate;
    }
    return QString::number(value, 'g', 17);
}
