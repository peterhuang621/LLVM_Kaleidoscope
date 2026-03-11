#include <iostream>

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

int main(int argc, char const* argv[]) {
    int Tok;
    do {
        Tok = get_tok();
        std::cout << Tok << '\n';
    } while (Tok != tok_eof);
    return 0;
}
