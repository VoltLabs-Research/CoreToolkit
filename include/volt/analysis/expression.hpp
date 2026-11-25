#ifndef VOLT_ANALYSIS_EXPRESSION_H
#define VOLT_ANALYSIS_EXPRESSION_H

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

struct Expr{
    ExprKind kind{ ExprKind::Number };

    double number{ 0.0 };
    std::string text;
    std::string property;
    BinaryOp binaryOp{ BinaryOp::Add };
    UnaryOp unaryOp{ UnaryOp::Neg };
    std::vector<Expr> children;
};

enum class DType{ F64, I32, I64, Str };

struct ColumnView{
    DType dtype{ DType::F64 };
    std::size_t componentCount{ 1 };
    const Particles::ParticleProperty* prop{ nullptr };
    const std::vector<std::string>* strValues{ nullptr };
};

struct AtomContext{
    std::size_t N{ 0 };
    int Frame{ 0 };
    double CellVolume{ 0.0 };
    std::function<std::optional<ColumnView>(const std::string&)> getColumn;
};

using Value = std::variant<double, std::string>;

[[nodiscard]] Expr parse(const std::string& formula);

[[nodiscard]] double evaluate(const Expr& expr, const AtomContext& context, std::size_t atomIndex);

[[nodiscard]] std::vector<double> evaluateColumn(const Expr& expr, const AtomContext& context);

[[nodiscard]] std::vector<char> evaluateSelection(const Expr& expr, const AtomContext& context);

[[nodiscard]] ColumnView columnFromProperty(const Particles::ParticleProperty& prop);

// The tokenizer and the evaluation of the parsed expressions.
namespace ExpressionDetail{

enum class TokenType{ Number, Identifier, String, Operator, LParen, RParen, Comma, Dot, Eof };

struct Token{
    TokenType type;
    std::string value;
    int line;
    int column;
};

bool isDigit(char ch);
bool isIdentStart(char ch);
bool isIdentPart(char ch);
std::vector<Token> tokenize(const std::string& input);
int binaryPrecedence(const std::string& op);
BinaryOp toBinaryOp(const std::string& op);
int componentIndex(const std::string& property);
double readColumnNumeric(const ColumnView& column, std::size_t atomIndex, int component);
double asNumber(const Value& value, const std::string& what);
double applyBuiltin(const std::string& name, const std::vector<double>& args);
Value evalVariable(const std::string& name, const AtomContext& context, std::size_t atomIndex);
Value evalMember(const std::string& object, const std::string& property, const AtomContext& context, std::size_t atomIndex);
Value evalBinary(const Expr& expr, const AtomContext& context, std::size_t atomIndex);
Value evalNode(const Expr& expr, const AtomContext& context, std::size_t atomIndex);

}

}

#endif
