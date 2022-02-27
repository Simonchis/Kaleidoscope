#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/*
-----------Lexer----------
The lexer returns token [0-255] if it is an unknown character, 
otherwise one of these for known things.
*/
enum Token {
    token_eof = -1,

    //commands
    token_def = -2,
    token_extern = -3,

    //primary
    token_identifier = -4,
    token_number = -5,
};

static std::string IdentifierStr;   //If tok_identifier, it will be filled
static double NumVal;               //If tok_number, it will be filled.

static int getToken(){
    static int LastChar = ' ';
    //skip blankspaces.
    while(isspace(LastChar)){
        LastChar = getchar();
    }

    // identifier: [a-zA-Z][a-zA-Z0-9]*
    if(isalpha(LastChar)){
        IdentifierStr = LastChar;
        while(isalnum(LastChar = getchar())){
            IdentifierStr += LastChar;
        }

        if(IdentifierStr == "def"){
            return token_def;
        }else if(IdentifierStr == "extern"){
            return token_extern;
        }else{
            return token_identifier;
        }
    }

    // Number: [0-9.]+
    if(isdigit(LastChar) || LastChar == '.'){
        std::string  NumStr;
        do{
            NumStr += LastChar;
            LastChar = getchar();
        }while(isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), nullptr);
        return token_number;
    }

    //skip comments
    if (LastChar == '#') {
    // Comment until end of line.
        do{
            LastChar = getchar();
        }while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF){
            return getToken();
        }
    }

    // Check for end of file.  Don't eat the EOF.
    if (LastChar == EOF){
        return token_eof;
    }

    // Otherwise, just return the character as its ascii value.
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}
