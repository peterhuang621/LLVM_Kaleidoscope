#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;

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
    virtual Value* codegen() = 0;
};

class NumberExprAST : public ExprAST {
    double Val;

public:
    NumberExprAST(double val) : Val(val) {}

    Value* codegen() override;
};

class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string& name) : Name(name) {}

    Value* codegen() override;
};

class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> Lhs,
                  std::unique_ptr<ExprAST> Rhs)
        : Op(op), LHS(std::move(Lhs)), RHS(std::move(Rhs)) {}

    Value* codegen() override;
};

class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

public:
    CallExprAST(const std::string& callee,
                std::vector<std::unique_ptr<ExprAST>> args)
        : Callee(callee), Args(std::move(args)) {}

    Value* codegen() override;
};

class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(const std::string& name, std::vector<std::string> args)
        : Name(name), Args(args) {}

    Function* codegen();
};

class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> proto,
                std::unique_ptr<ExprAST> body)
        : Proto(std::move(proto)), Body(std::move(body)) {}

    Function* codegen();
};

//=== parser
static int CurTok;
static int getNextToken() {
    return CurTok = get_tok();
}

static std::unordered_map<char, int> BinopPrecedence;
static int GetTokPrecedence() {
    if (!isascii(CurTok)) return -1;

    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0) return -1;

    return TokPrec;
}

std::unique_ptr<ExprAST> LogError(const char* Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* Str) {
    LogError(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken();
    auto V = ParseExpression();
    if (!V) return nullptr;

    if (CurTok != ')') return LogError("expected ')'");
    getNextToken();
    return V;
}

// identifierexpr
// ::= identifier
// ::= identifier '(' expression * ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;
    getNextToken();

    // variable
    if (CurTok != '(') return std::make_unique<VariableExprAST>(IdName);

    // call
    getNextToken();
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression())
                Args.emplace_back(std::move(Arg));
            else
                return nullptr;

            if (CurTok == ')') break;
            if (CurTok != ',')
                return LogError("expected ')' or ',' in argument list");
            getNextToken();
        }
    }

    getNextToken();
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

//=== primary
// ::= identifier
// ::= numberexpr
// ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
        default:
            return LogError("unknown token when expecting an expression");
    }
}

// binoprhs ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecedence();
        if (TokPrec < ExprPrec) return LHS;

        int BinOP = CurTok;
        getNextToken();

        auto RHS = ParsePrimary();
        if (!RHS) return nullptr;

        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS) return nullptr;
        }

        LHS = std::make_unique<BinaryExprAST>(BinOP, std::move(LHS),
                                              std::move(RHS));
    }
}

// expression ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS) return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

// prototype ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if (CurTok != tok_identifier)
        return LogErrorP("expected function name in prototype");

    std::string fname = IdentifierStr;
    getNextToken();

    if (CurTok != '(') return LogErrorP("expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.emplace_back(IdentifierStr);
    if (CurTok != ')') return LogErrorP("expected ')' in prototype");

    getNextToken();

    return std::make_unique<PrototypeAST>(fname, std::move(ArgNames));
}

// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken();
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                    std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken();
    return ParsePrototype();
}

//=== Codegen
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unordered_map<std::string, Value*> NamedValues;

Value* LogErrorV(const char* Str) {
    LogError(Str);
    return nullptr;
}

Value* NumberExprAST::codegen() {
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value* VariableExprAST::codegen() {
    Value* V = NamedValues[Name];
    if (!V) return LogErrorV("unknown variable name");
    return V;
}

Value* BinaryExprAST::codegen() {
    Value *L = LHS->codegen(), *R = RHS->codegen();
    if (!L || !R) return nullptr;
    switch (Op) {
        case '+':
            return Builder->CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder->CreateFSub(L, R, "subtmp");
        case '*':
            return Builder->CreateFMul(L, R, "multmp");
        case '<':
            L = Builder->CreateFCmpOLT(L, R, "cmptmp");
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                         "booltmp");
        default:
            return LogErrorV("invalid binary operator");
    }
}

Value* CallExprAST::codegen() {
    Function* CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF) return LogErrorV("unknown function referenced");
    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("incorrect # arguments passed");
    std::vector<Value*> ArgsV;
    Value* TmpArg = nullptr;
    for (unsigned i = 0, e = Args.size(); i != e; i++) {
        TmpArg = Args[i]->codegen();
        if (!TmpArg) return nullptr;
        ArgsV.emplace_back(TmpArg);
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function* PrototypeAST::codegen() {
    // std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
}

//=== TopLevel
static void HandleDefinition() {
    if (ParseDefinition())
        fprintf(stderr, "parsed a function definition.\n");
    else
        getNextToken();
}

static void HandleExtern() {
    if (ParseExtern())
        fprintf(stderr, "parsed an extern.\n");
    else
        getNextToken();
}

static void HandleTopLevelExpression() {
    if (ParseTopLevelExpr())
        fprintf(stderr, "parsed a top-level expr.\n");
    else
        getNextToken();
}

// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
            case tok_eof:
                return;
            case ';':
                getNextToken();
                break;
            case tok_def:
                HandleDefinition();
                break;
            case tok_extern:
                HandleExtern();
                break;
            default:
                HandleTopLevelExpression();
                break;
        }
    }
}

//=== Driver
int main(int argc, char const* argv[]) {
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    BinopPrecedence['/'] = 40;

    fprintf(stderr, "ready> ");
    getNextToken();

    MainLoop();

    return 0;
}
