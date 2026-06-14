#include <volt/analysis/expression.hpp>

#include <array>
#include <cmath>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

// C++23 mirror of @voltstack/expressions. Kept structurally parallel to
// the TypeScript parser/evaluator so the two stay in lockstep.

namespace Volt::Analysis{

namespace{

enum class TokenType{ Number, Identifier, String, Operator, LParen, RParen, Comma, Dot, Eof };

struct Token{
    TokenType type;
    std::string value;
    int line;
    int column;
};

bool isDigit(char ch){ return ch >= '0' && ch <= '9'; }
bool isIdentStart(char ch){ return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; }
bool isIdentPart(char ch){ return isIdentStart(ch) || isDigit(ch); }

std::vector<Token> tokenize(const std::string& input){
    std::vector<Token> tokens;
    std::size_t index = 0;
    int line = 1;
    int column = 1;

    auto advance = [&](int count = 1){
        for(int step = 0; step < count; ++step){
            if(index < input.size() && input[index] == '\n'){
                line += 1;
                column = 1;
            }else{
                column += 1;
            }
            index += 1;
        }
    };

    auto peekAt = [&](std::size_t offset) -> char{
        std::size_t pos = index + offset;
        return pos < input.size() ? input[pos] : '\0';
    };

    while(index < input.size()){
        char ch = input[index];

        if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'){
            advance();
            continue;
        }

        int startLine = line;
        int startColumn = column;

        if(ch == '('){ tokens.push_back({ TokenType::LParen, "(", startLine, startColumn }); advance(); continue; }
        if(ch == ')'){ tokens.push_back({ TokenType::RParen, ")", startLine, startColumn }); advance(); continue; }
        if(ch == ','){ tokens.push_back({ TokenType::Comma, ",", startLine, startColumn }); advance(); continue; }

        // Numbers: integer, decimal, scientific notation. Checked before the
        // lone-dot branch so a leading-dot literal like '.5' lexes as a number.
        if(isDigit(ch) || (ch == '.' && isDigit(peekAt(1)))){
            std::string raw;
            while(index < input.size() && isDigit(input[index])){ raw += input[index]; advance(); }
            if(index < input.size() && input[index] == '.'){
                raw += '.';
                advance();
                while(index < input.size() && isDigit(input[index])){ raw += input[index]; advance(); }
            }
            if(index < input.size() && (input[index] == 'e' || input[index] == 'E')){
                raw += input[index];
                advance();
                if(index < input.size() && (input[index] == '+' || input[index] == '-')){ raw += input[index]; advance(); }
                if(!isDigit(peekAt(0))){
                    throw ExpressionError("malformed number literal '" + raw + "'", startLine, startColumn);
                }
                while(index < input.size() && isDigit(input[index])){ raw += input[index]; advance(); }
            }
            tokens.push_back({ TokenType::Number, raw, startLine, startColumn });
            continue;
        }

        // Member-access dot (after the number branch, so '.5' is a number).
        if(ch == '.'){ tokens.push_back({ TokenType::Dot, ".", startLine, startColumn }); advance(); continue; }

        // Quoted strings: variable type names ("Cu") and string literals.
        if(ch == '"' || ch == '\''){
            char quote = ch;
            advance();
            std::string raw;
            while(index < input.size() && input[index] != quote){
                if(input[index] == '\\' && index + 1 < input.size()){
                    advance();
                    raw += input[index];
                    advance();
                    continue;
                }
                raw += input[index];
                advance();
            }
            if(index >= input.size() || input[index] != quote){
                throw ExpressionError("unterminated string literal", startLine, startColumn);
            }
            advance();
            tokens.push_back({ TokenType::String, raw, startLine, startColumn });
            continue;
        }

        if(isIdentStart(ch)){
            std::string raw;
            while(index < input.size() && isIdentPart(input[index])){ raw += input[index]; advance(); }
            tokens.push_back({ TokenType::Identifier, raw, startLine, startColumn });
            continue;
        }

        // Operators.
        {
            static const std::unordered_set<char> operatorChars{
                '+', '-', '*', '/', '^', '=', '!', '<', '>', '&', '|'
            };
            if(operatorChars.contains(ch)){
                char next = peekAt(1);
                std::string two{ ch, next };
                if(two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "&&" || two == "||"){
                    tokens.push_back({ TokenType::Operator, two, startLine, startColumn });
                    advance(2);
                    continue;
                }
                if(ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^' || ch == '<' || ch == '>' || ch == '!'){
                    tokens.push_back({ TokenType::Operator, std::string(1, ch), startLine, startColumn });
                    advance();
                    continue;
                }
                std::string hint{ ch, ch };
                throw ExpressionError(std::string("unexpected character '") + ch + "' (did you mean '" + hint + "'?)", startLine, startColumn);
            }
        }

        throw ExpressionError(std::string("unexpected character '") + ch + "'", startLine, startColumn);
    }

    tokens.push_back({ TokenType::Eof, "", line, column });
    return tokens;
}

int binaryPrecedence(const std::string& op){
    if(op == "||") return 1;
    if(op == "&&") return 2;
    if(op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
    if(op == "+" || op == "-") return 4;
    if(op == "*" || op == "/") return 5;
    if(op == "^") return 6;
    return -1;
}

BinaryOp toBinaryOp(const std::string& op){
    if(op == "+") return BinaryOp::Add;
    if(op == "-") return BinaryOp::Sub;
    if(op == "*") return BinaryOp::Mul;
    if(op == "/") return BinaryOp::Div;
    if(op == "^") return BinaryOp::Pow;
    if(op == "==") return BinaryOp::Eq;
    if(op == "!=") return BinaryOp::Ne;
    if(op == "<") return BinaryOp::Lt;
    if(op == ">") return BinaryOp::Gt;
    if(op == "<=") return BinaryOp::Le;
    if(op == ">=") return BinaryOp::Ge;
    if(op == "&&") return BinaryOp::And;
    return BinaryOp::Or;
}

class Parser{
public:
    explicit Parser(std::vector<Token> tokens) : _tokens(std::move(tokens)){}

    Expr parse(){
        if(peek().type == TokenType::Eof){
            throw ExpressionError("empty expression", 1, 1);
        }
        Expr expr = parseExpression(0);
        const Token& trailing = peek();
        if(trailing.type != TokenType::Eof){
            throw ExpressionError("unexpected '" + trailing.value + "' after expression", trailing.line, trailing.column);
        }
        return expr;
    }

private:
    std::vector<Token> _tokens;
    std::size_t _position = 0;

    const Token& peek() const{ return _tokens[_position]; }
    const Token& next(){ return _tokens[_position++]; }

    const Token& expect(TokenType type, const std::string& description){
        const Token& token = peek();
        if(token.type != type){
            std::string found = token.value.empty() ? "end of input" : token.value;
            throw ExpressionError("expected " + description + " but found '" + found + "'", token.line, token.column);
        }
        return next();
    }

    Expr parseExpression(int minPrecedence){
        Expr left = parseUnary();

        for(;;){
            const Token& token = peek();
            if(token.type != TokenType::Operator) break;
            int precedence = binaryPrecedence(token.value);
            if(precedence < 0 || precedence < minPrecedence) break;

            std::string op = token.value;
            next();
            // '^' is right-associative; everything else left-associative.
            int nextMin = (op == "^") ? precedence : precedence + 1;
            Expr right = parseExpression(nextMin);

            Expr node;
            node.kind = ExprKind::Binary;
            node.binaryOp = toBinaryOp(op);
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            left = std::move(node);
        }

        return left;
    }

    Expr parseUnary(){
        const Token& token = peek();
        if(token.type == TokenType::Operator && (token.value == "-" || token.value == "!")){
            std::string op = token.value;
            next();
            Expr node;
            node.kind = ExprKind::Unary;
            node.unaryOp = (op == "-") ? UnaryOp::Neg : UnaryOp::Not;
            node.children.push_back(parseUnary());
            return node;
        }
        return parsePrimary();
    }

    Expr parsePrimary(){
        const Token& token = peek();

        if(token.type == TokenType::Number){
            Expr node;
            node.kind = ExprKind::Number;
            node.number = std::stod(token.value);
            next();
            return node;
        }

        if(token.type == TokenType::String){
            Expr node;
            node.kind = ExprKind::StringLiteral;
            node.text = token.value;
            next();
            return node;
        }

        if(token.type == TokenType::LParen){
            next();
            Expr inner = parseExpression(0);
            expect(TokenType::RParen, "')'");
            return inner;
        }

        if(token.type == TokenType::Identifier){
            std::string name = token.value;
            next();

            // Function call.
            if(peek().type == TokenType::LParen){
                next();
                Expr node;
                node.kind = ExprKind::Call;
                node.text = name;
                if(peek().type != TokenType::RParen){
                    node.children.push_back(parseExpression(0));
                    while(peek().type == TokenType::Comma){
                        next();
                        node.children.push_back(parseExpression(0));
                    }
                }
                expect(TokenType::RParen, "')' to close argument list");
                return node;
            }

            // Member access: Position.X
            if(peek().type == TokenType::Dot){
                next();
                const Token& property = expect(TokenType::Identifier, "property name after .");
                Expr node;
                node.kind = ExprKind::Member;
                node.text = name;
                node.property = property.value;
                return node;
            }

            // Boolean literals.
            if(name == "true" || name == "false"){
                Expr node;
                node.kind = ExprKind::Number;
                node.number = (name == "true") ? 1.0 : 0.0;
                return node;
            }

            Expr node;
            node.kind = ExprKind::Variable;
            node.text = name;
            return node;
        }

        std::string found = token.value.empty() ? "end of input" : token.value;
        throw ExpressionError("expected a value but found '" + found + "'", token.line, token.column);
    }
};

// ---- Evaluation ----

const std::array<const char*, 3> COMPONENT_NAMES{ "X", "Y", "Z" };

int componentIndex(const std::string& property){
    for(int i = 0; i < 3; ++i){
        if(property == COMPONENT_NAMES[i]) return i;
    }
    return -1;
}

double readColumnNumeric(const ColumnView& column, std::size_t atomIndex, int component){
    const auto* prop = column.prop;
    switch(column.dtype){
        case DType::F64:
            return prop->getDoubleComponent(atomIndex, static_cast<std::size_t>(component));
        case DType::I32:
            return static_cast<double>(prop->getIntComponent(atomIndex, static_cast<std::size_t>(component)));
        case DType::I64:
            return static_cast<double>(
                prop->getInt64Component(atomIndex, static_cast<std::size_t>(component)));
        case DType::Str:
            throw ExpressionError("cannot read string column as a number");
    }
    return 0.0;
}

double asNumber(const Value& value, const std::string& what){
    if(std::holds_alternative<std::string>(value)){
        throw ExpressionError("cannot use string column/value '" + what + "' in arithmetic");
    }
    return std::get<double>(value);
}

double applyBuiltin(const std::string& name, const std::vector<double>& args){
    auto arity = [&](int expected){
        if(static_cast<int>(args.size()) != expected){
            throw ExpressionError(name + "() expects " + std::to_string(expected) +
                " argument(s), got " + std::to_string(args.size()));
        }
    };

    if(name == "sqrt"){ arity(1); return std::sqrt(args[0]); }
    if(name == "abs"){ arity(1); return std::abs(args[0]); }
    if(name == "sin"){ arity(1); return std::sin(args[0]); }
    if(name == "cos"){ arity(1); return std::cos(args[0]); }
    if(name == "tan"){ arity(1); return std::tan(args[0]); }
    if(name == "exp"){ arity(1); return std::exp(args[0]); }
    if(name == "log"){ arity(1); return std::log(args[0]); }
    if(name == "floor"){ arity(1); return std::floor(args[0]); }
    if(name == "ceil"){ arity(1); return std::ceil(args[0]); }
    if(name == "round"){ arity(1); return std::round(args[0]); }

    if(name == "min"){
        if(args.empty()) throw ExpressionError("min() expects at least 1 argument");
        double m = args[0];
        for(double v : args) m = std::min(m, v);
        return m;
    }
    if(name == "max"){
        if(args.empty()) throw ExpressionError("max() expects at least 1 argument");
        double m = args[0];
        for(double v : args) m = std::max(m, v);
        return m;
    }
    if(name == "clamp"){
        arity(3);
        return std::min(std::max(args[0], args[1]), args[2]);
    }
    if(name == "if"){
        arity(3);
        return args[0] != 0.0 ? args[1] : args[2];
    }
    if(name == "length"){
        if(args.empty()) throw ExpressionError("length() expects at least 1 argument");
        double sum = 0.0;
        for(double v : args) sum += v * v;
        return std::sqrt(sum);
    }

    throw ExpressionError("unknown function '" + name + "'");
}

Value evalNode(const Expr& expr, const AtomContext& context, std::size_t atomIndex);

Value evalVariable(const std::string& name, const AtomContext& context, std::size_t atomIndex){
    if(name == "ParticleIndex") return static_cast<double>(atomIndex);
    if(name == "N") return static_cast<double>(context.N);
    if(name == "Frame") return static_cast<double>(context.Frame);
    if(name == "CellVolume") return context.CellVolume;

    auto column = context.getColumn(name);
    if(!column){
        throw ExpressionError("unknown variable or column '" + name + "'");
    }
    if(column->dtype == DType::Str){
        return (*column->strValues)[atomIndex];
    }
    if(column->componentCount != 1){
        throw ExpressionError("'" + name + "' has " + std::to_string(column->componentCount) +
            " components; use " + name + ".X / " + name + ".Y / " + name + ".Z");
    }
    return readColumnNumeric(*column, atomIndex, 0);
}

Value evalMember(const std::string& object, const std::string& property, const AtomContext& context, std::size_t atomIndex){
    int component = componentIndex(property);
    if(component < 0){
        throw ExpressionError("unknown component '" + object + "." + property + "' (expected X, Y or Z)");
    }
    auto column = context.getColumn(object);
    if(!column){
        throw ExpressionError("unknown column '" + object + "'");
    }
    if(static_cast<std::size_t>(component) >= column->componentCount){
        throw ExpressionError("'" + object + "' has " + std::to_string(column->componentCount) +
            " component(s); '." + property + "' is out of range");
    }
    return readColumnNumeric(*column, atomIndex, component);
}

Value evalBinary(const Expr& expr, const AtomContext& context, std::size_t atomIndex){
    BinaryOp op = expr.binaryOp;
    const Expr& leftExpr = expr.children[0];
    const Expr& rightExpr = expr.children[1];

    // Short-circuit logical operators.
    if(op == BinaryOp::And){
        double left = asNumber(evalNode(leftExpr, context, atomIndex), "&&");
        if(left == 0.0) return 0.0;
        return asNumber(evalNode(rightExpr, context, atomIndex), "&&") != 0.0 ? 1.0 : 0.0;
    }
    if(op == BinaryOp::Or){
        double left = asNumber(evalNode(leftExpr, context, atomIndex), "||");
        if(left != 0.0) return 1.0;
        return asNumber(evalNode(rightExpr, context, atomIndex), "||") != 0.0 ? 1.0 : 0.0;
    }

    Value left = evalNode(leftExpr, context, atomIndex);
    Value right = evalNode(rightExpr, context, atomIndex);

    // Equality works on strings (type-name matching) and numbers.
    if(op == BinaryOp::Eq) return (left == right) ? 1.0 : 0.0;
    if(op == BinaryOp::Ne) return (left != right) ? 1.0 : 0.0;

    double lhs = asNumber(left, "left operand");
    double rhs = asNumber(right, "right operand");

    switch(op){
        case BinaryOp::Add: return lhs + rhs;
        case BinaryOp::Sub: return lhs - rhs;
        case BinaryOp::Mul: return lhs * rhs;
        case BinaryOp::Div:
            if(rhs == 0.0) throw ExpressionError("division by zero");
            return lhs / rhs;
        case BinaryOp::Pow: return std::pow(lhs, rhs);
        case BinaryOp::Lt: return lhs < rhs ? 1.0 : 0.0;
        case BinaryOp::Gt: return lhs > rhs ? 1.0 : 0.0;
        case BinaryOp::Le: return lhs <= rhs ? 1.0 : 0.0;
        case BinaryOp::Ge: return lhs >= rhs ? 1.0 : 0.0;
        default: return 0.0;
    }
}

Value evalNode(const Expr& expr, const AtomContext& context, std::size_t atomIndex){
    switch(expr.kind){
        case ExprKind::Number:
            return expr.number;
        case ExprKind::StringLiteral:
            return expr.text;
        case ExprKind::Variable:
            return evalVariable(expr.text, context, atomIndex);
        case ExprKind::Member:
            return evalMember(expr.text, expr.property, context, atomIndex);
        case ExprKind::Unary: {
            double operand = asNumber(evalNode(expr.children[0], context, atomIndex), "operand");
            if(expr.unaryOp == UnaryOp::Neg) return -operand;
            return operand == 0.0 ? 1.0 : 0.0;
        }
        case ExprKind::Binary:
            return evalBinary(expr, context, atomIndex);
        case ExprKind::Call: {
            std::vector<double> args;
            args.reserve(expr.children.size());
            for(const Expr& arg : expr.children){
                args.push_back(asNumber(evalNode(arg, context, atomIndex), expr.text));
            }
            return applyBuiltin(expr.text, args);
        }
    }
    return 0.0;
}

}

Expr parse(const std::string& formula){
    Parser parser(tokenize(formula));
    return parser.parse();
}

double evaluate(const Expr& expr, const AtomContext& context, std::size_t atomIndex){
    Value result = evalNode(expr, context, atomIndex);
    if(std::holds_alternative<std::string>(result)){
        throw ExpressionError("expression resolved to a string; expected a numeric or boolean result");
    }
    return std::get<double>(result);
}

std::vector<double> evaluateColumn(const Expr& expr, const AtomContext& context){
    std::vector<double> out;
    out.reserve(context.N);
    for(std::size_t i = 0; i < context.N; ++i){
        out.push_back(evaluate(expr, context, i));
    }
    return out;
}

std::vector<char> evaluateSelection(const Expr& expr, const AtomContext& context){
    std::vector<char> out;
    out.reserve(context.N);
    for(std::size_t i = 0; i < context.N; ++i){
        out.push_back(evaluate(expr, context, i) != 0.0 ? 1 : 0);
    }
    return out;
}

ColumnView columnFromProperty(const Particles::ParticleProperty& prop){
    ColumnView view;
    view.prop = &prop;
    view.componentCount = prop.componentCount() == 0 ? 1 : prop.componentCount();
    switch(prop.dataType()){
        case Particles::DataType::Double: view.dtype = DType::F64; break;
        case Particles::DataType::Int: view.dtype = DType::I32; break;
        case Particles::DataType::Int64: view.dtype = DType::I64; break;
        case Particles::DataType::Void: view.dtype = DType::F64; break;
    }
    return view;
}

}
