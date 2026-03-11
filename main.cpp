#include <iostream>
#include <map>
#include <memory>
#include <utility>
#include <vector>

//=== Lexer
enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5
};

static std::string IdentifierStr;
static double NumVal;

static int get_tok() {
    static int LastChar = ' ';

    // skip white
    while (isspace(LastChar)) {
        LastChar = getchar();
    }

    // keyword & identifier
    if (isalpha(LastChar)) {
        IdentifierStr = LastChar;
        while (isalnum(LastChar = getchar())) {
            IdentifierStr += LastChar;
        }

        // keyword
        if (IdentifierStr == "def") return tok_def;
        if (IdentifierStr == "extern") return tok_extern;

        // identifier
        return tok_identifier;
    }

    // number
    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;

        bool hasDot = false;
        do {
            // multiple dots to be ignored
            if (!(hasDot && LastChar == '.')) NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0);
        return tok_number;
    }

    // comment
    if (LastChar == '#') {
        do LastChar = getchar();
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
        if (LastChar != EOF) return get_tok();
    }

    // end
    if (LastChar == EOF) return tok_eof;

    // other character
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

//=== AST
class ExprAST {
public:
    virtual ~ExprAST() = default;
};

class NumberExprAST : public ExprAST {
    double Val;

public:
    NumberExprAST(double val) : Val(val) {}
};

class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string& name) : Name(name) {}
};

class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> Lhs,
                  std::unique_ptr<ExprAST> Rhs)
        : Op(op), LHS(std::move(Lhs)), RHS(std::move(Rhs)) {}
};

class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

public:
    CallExprAST(const std::string& callee,
                std::vector<std::unique_ptr<ExprAST>> args)
        : Callee(callee), Args(args) {}
};

class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(const std::string& name, std::vector<std::string> args)
        : Name(name), Args(args) {}
};

class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> proto,
                std::unique_ptr<ExprAST> body)
        : Proto(std::move(proto)), Body(std::move(body)) {}
};

int main(int argc, char const* argv[]) {
    int Tok;
    do {
        Tok = get_tok();
        std::cout << Tok << '\n';
    } while (Tok != tok_eof);
    return 0;
}
