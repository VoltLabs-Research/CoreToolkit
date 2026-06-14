#ifndef VOLT_ANALYSIS_EXPRESSION_H
#define VOLT_ANALYSIS_EXPRESSION_H

// C++23 mirror of @voltstack/expressions.
//
// Same grammar, operator precedence, builtin set and OVITO variable namespace
// as the TypeScript engine, so client/daemon (TS) and plugin (C++) evaluation
// produce byte-identical results. The AST is a recursive tagged struct (no
// polymorphism, no virtuals); evaluation walks it once per atom index.

#include <volt/core/volt.h>
#include <volt/core/particle_property.h>
#include <volt/core/simulation_cell.h>

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <functional>
#include <stdexcept>
#include <cstdint>

namespace Volt::Analysis{

// Thrown for any tokenize/parse/evaluate failure. Carries source coordinates.
class ExpressionError : public std::runtime_error{
public:
    int line;
    int column;

    explicit ExpressionError(const std::string& message, int line_ = 1, int column_ = 1)
        : std::runtime_error(message), line(line_), column(column_){}
};

enum class BinaryOp{
    Add, Sub, Mul, Div, Pow,
    Eq, Ne, Lt, Gt, Le, Ge,
    And, Or
};

enum class UnaryOp{ Neg, Not };

enum class ExprKind{ Number, StringLiteral, Variable, Member, Binary, Unary, Call };

// Recursive AST node. `children` holds operands:
//   Binary → [left, right]   Unary → [operand]   Call → args...
struct Expr{
    ExprKind kind{ ExprKind::Number };

    double number{ 0.0 };       // Number literal
    std::string text;           // StringLiteral value / Variable name / Member object / Call func
    std::string property;       // Member property (X/Y/Z)
    BinaryOp binaryOp{ BinaryOp::Add };
    UnaryOp unaryOp{ UnaryOp::Neg };
    std::vector<Expr> children;
};

enum class DType{ F64, I32, I64, Str };

// A typed column view. Numeric columns reference a ParticleProperty (no copy);
// string columns reference a vector<string>. Typed columns stay typed through
// evaluation — coercion to double happens only when an operator forces it.
struct ColumnView{
    DType dtype{ DType::F64 };
    std::size_t componentCount{ 1 };
    const Particles::ParticleProperty* prop{ nullptr };
    const std::vector<std::string>* strValues{ nullptr };
};

// The OVITO variable namespace plus per-column access, mirroring the TS
// AtomContext. `getColumn` bridges named variables to ParticleProperty data.
struct AtomContext{
    std::size_t N{ 0 };
    int Frame{ 0 };
    double CellVolume{ 0.0 };
    std::function<std::optional<ColumnView>(const std::string&)> getColumn;
};

// Evaluated value: number (booleans flow as 1.0/0.0) or string (type-name
// matching via == / != only).
using Value = std::variant<double, std::string>;

// Tokenizer + Pratt parser → AST. Throws ExpressionError on malformed input.
[[nodiscard]] Expr parse(const std::string& formula);

// Evaluate a parsed expression for a single atom. Booleans return 1.0 / 0.0.
[[nodiscard]] double evaluate(const Expr& expr, const AtomContext& context, std::size_t atomIndex);

// Convenience: evaluate across all N atoms into a derived column.
[[nodiscard]] std::vector<double> evaluateColumn(const Expr& expr, const AtomContext& context);

// Convenience: evaluate a boolean selection across all N atoms.
[[nodiscard]] std::vector<char> evaluateSelection(const Expr& expr, const AtomContext& context);

// Helper to build a ColumnView over a ParticleProperty, inferring DType from
// its DataType and using its component count.
[[nodiscard]] ColumnView columnFromProperty(const Particles::ParticleProperty& prop);

}

#endif
