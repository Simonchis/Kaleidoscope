#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
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
        }
        if(IdentifierStr == "extern"){
            return token_extern;
        }
        
        return token_identifier;
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


/*
------------AST-----------
There are expression AST, prototype AST and funtion AST.
*/
namespace{

//ExprAST - Base class for all expression nodes.
class ExprAST{
public:
    virtual ~ExprAST() = default;
    virtual Value* codeGen() = 0;
};

//NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST{
private:
    double Val;
public:
    NumberExprAST(double val) : Val(val) {}
    Value* codeGen() override;
};

//VaribleExprAST - Expression class for referencing a varible, like "a".
class VariableExprAST : public ExprAST{
private:
    std::string Name;
public:
    VariableExprAST(const std::string& name) : Name(name) {}
    Value* codeGen() override;
};

//BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST{
private:
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;
public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> lhs, std::unique_ptr<ExprAST> rhs)
        : Op(op), LHS(std::move(lhs)), RHS(std::move(rhs)) {}
    Value* codeGen() override;
};

//CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST{
private:
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;
public:
    CallExprAST(const std::string& callee, std::vector<std::unique_ptr<ExprAST>> args)
        : Callee(callee), Args(std::move(args)) {}
    Value* codeGen() override;
};


//PrototypeAST - This calss represents the prototype for a function,
//which captures its name, and its arguments names(and number)
class PrototypeAST{
private:
    std::string Name;
    std::vector<std::string> Args;
public:
    PrototypeAST(const std::string& name, std::vector<std::string> args)
        : Name(name), Args(std::move(args)) {}
    const std::string& getName() const {return Name;}
    Function* codeGen();
};


//FunctinoAST - This class represents a function definition itself.
class FunctionAST{
private:
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;
public:
    FunctionAST(std::unique_ptr<PrototypeAST> proto, std::unique_ptr<ExprAST> body)
        : Proto(std::move(proto)), Body(std::move(body)) {}
    Function* codeGen();
};

}// end anonymous namespace.

/*
-----------Parser----------
*/
//CurToken is the current token the parser is looking at,
//getNextTOken reads next token from lexer and updates CurToken.
static int CurToken;
static int getNextToken(){
    return CurToken = getToken();
}

//Holds the precedence for each binary operator that is defined.
static std::map<char, int> BinOpPrecedence;
static int GetTokenPrecedence(){
    if(!isascii(CurToken)){
        return -1;
    }

    //it must be a declared binop.
    int TokenPrec = BinOpPrecedence[CurToken];
    if(TokenPrec <= 0){
        return -1;
    }
    return TokenPrec;
}

//LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str){
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str){
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

//numberExpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr(){
    auto result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();//consume the number
    return std::move(result);
}

//parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr(){
    getNextToken(); //eat '('
    auto V = ParseExpression();
    if(!V){
        return nullptr;
    }
    if(CurToken != ')'){
        return LogError("expected ')'");
    }
    getNextToken(); //eat ')'
    return V;
}

//identifierexpr
//  ::= identifier
//  ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr(){
    std::string IdName = IdentifierStr;
    getNextToken();//est identifier

    //simple varible
    if(CurToken != '('){
        return std::make_unique<VariableExprAST>(IdName);
    }

    //Call
    getNextToken();//eat '('
    std::vector<std::unique_ptr<ExprAST>> Args;
    if(CurToken != ')'){
        while(true){
            if(auto Arg = ParseExpression()){
                Args.push_back(std::move(Arg));
            }else{
                return nullptr;
            }
            if(CurToken == ')'){
                break;
            }
            if(CurToken != ','){
                return LogError("expected ')' or ',' in argument list");
            }
            getNextToken();
        }
    }

    //eat ')'
    getNextToken();
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

//primary
//  ::= identifierexpr
//  ::= numberexpr
//  ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary(){
    switch(CurToken){
        case token_identifier : return ParseIdentifierExpr();
        case token_number : return ParseNumberExpr();
        case '(' : return ParseParenExpr();
        default : return LogError("unknown token when expecting an expression");
    }
}

//binoprhs ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS){
    //If this is a binary op, find its precedence.
    while(true){
        int TokenPrec = GetTokenPrecedence();

        //If this is a binop that binds at least as tightly as the current binop,
        //consume it, otherwise we are done.
        if(TokenPrec < ExprPrec){
            return LHS;
        }

        int BinOp = CurToken;
        getNextToken();//eat the binop

        //Parse the primary expression after the binary operator.
        auto RHS = ParsePrimary();
        if(!RHS){
            return nullptr;
        }

        //If BinOp binds less tightly with RHS than the operator after RHS, let
        //the pending operator take RHS as its LHS.
        int NextPrec = GetTokenPrecedence();
        if(TokenPrec < NextPrec){
            RHS = ParseBinOpRHS(TokenPrec + 1, std::move(RHS));
            if(!RHS){
                return nullptr;
            }
        }

        //Merge LHS/RHS
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

//expression ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression(){
    auto LHS = ParsePrimary();
    if(!LHS){
        return nullptr;
    }
    return ParseBinOpRHS(0, std::move(LHS));
}

//prototype ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype(){
    if(CurToken != token_identifier){
        return LogErrorP("Expected function name in prototype");
    }
    std::string FnName = IdentifierStr;
    getNextToken();//eat function name

    if(CurToken != '('){
        return LogErrorP("Expected '(' in prototype");
    }

    std::vector<std::string> ArgNames;
    while(getNextToken() == token_identifier){
        ArgNames.push_back(IdentifierStr);
    }
    if(CurToken != ')'){
        return LogErrorP("Expected ')' in prototype");
    }

    //success
    getNextToken();//eat ')'
    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

//definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinifion(){
    getNextToken();//eat def
    auto Proto = ParsePrototype();
    if(!Proto){
        return nullptr;
    }
    if(auto E = ParseExpression()){
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

//toplevelexp ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr(){
    if(auto E = ParseExpression()){
        //make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

//external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern(){
    getNextToken(); //eat extern;
    return ParsePrototype();
}

/*
------------Code Generation-----------
*/
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value*> NamedValues;

Value* LogErrorV(const char* Str){
    LogError(Str);
    return nullptr;
}

Value* NumberExprAST::codeGen(){
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value* VariableExprAST::codeGen(){
    //Look up this varible in the function.
    Value* V = NamedValues[Name];
    if(!V){
        return LogErrorV("Unknown varible name");
    }
    return V;
}

Value* BinaryExprAST::codeGen(){
    Value* L = LHS->codeGen();
    Value* R = RHS->codeGen();
    if(!L || !R){
        return nullptr;
    }
    switch(Op){
        case '+': return Builder->CreateFAdd(L, R, "addtmp");
        case '-': return Builder->CreateFSub(L, R, "subtmp");
        case '*': return Builder->CreateFMul(L, R, "multmp");
        case '<': 
            L = Builder->CreateFCmpULT(L, R, "cmptmp");
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
        default: return LogErrorV("invalid binary operator");
    }
}

Value* CallExprAST::codeGen(){
    //Look up the name in the global module table
    Function* CalleeF = TheModule->getFunction(Callee);
    if(!CalleeF){
        return LogErrorV("Unknown function referenced");
    }
    //If argument mismatch error
    if(CalleeF->arg_size() != Args.size()){
        return LogErrorV("Incorrect # arguments passed");
    }
    std::vector<Value*> ArgsV;
    for(unsigned i = 0, e = Args.size(); i != e; ++i){
        ArgsV.push_back(Args[i]->codeGen());
        if(!ArgsV.back()){
            return nullptr;
        }
    }
    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function* PrototypeAST::codeGen(){
    //Make the function type : double(double, double) etc;
    std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
    FunctionType* FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);
    Function* F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
    //Set names for all arguments.
    unsigned Idx = 0;
    for(auto& Arg : F->args()){
        Arg.setName(Args[Idx++]);
    }
    return F;
}

Function *FunctionAST::codeGen() {
    // First, check for an existing function from a previous 'extern' declaration.
    Function *TheFunction = TheModule->getFunction(Proto->getName());

    if(!TheFunction){
        TheFunction = Proto->codeGen();
    }
    if(!TheFunction){
        return nullptr; 
    }
    // Create a new basic block to start insertion into.
    BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    NamedValues.clear();
    for (auto &Arg : TheFunction->args()){
        NamedValues[std::string(Arg.getName())] = &Arg;
    }

    if (Value *RetVal = Body->codeGen()){
        // Finish off the function.
        Builder->CreateRet(RetVal);

        // Validate the generated code, checking for consistency.
        verifyFunction(*TheFunction);

        return TheFunction;
    }

    // Error reading body, remove function.
    TheFunction->eraseFromParent();
    return nullptr;
}

/*
-----------Top-Level Parsing and JIT Driver----------
*/
static void InitializeModule(){
    //Open a new context and module.
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("My JIT", *TheContext);
    //Create a new builder for the module.
    Builder = std::make_unique<IRBuilder<>>(*TheContext);
}

static void HandleDefinition(){
    if(auto FnAST = ParseDefinifion()){
        if(auto *FnIR = FnAST->codeGen()){
            fprintf(stderr, "Parsed a function definition.\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
        }
    }else{
        //skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern(){
    if(auto ProtoAST = ParseExtern()){
        if(auto *FnIR = ProtoAST->codeGen()){
            fprintf(stderr, "Parsed an extern.\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
        }
    }else{
        //skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression(){
    // Evaluate a top-level expression into an anonymous function.
    if(auto FnAST =  ParseTopLevelExpr()){
        if(auto *FnIR = FnAST->codeGen()){
           fprintf(stderr, "Parsed a top-level expr.\n");
           FnIR->print(errs());
           fprintf(stderr, "\n");
           //Remove the anonymous expression
           FnIR->eraseFromParent();
        }
    }else{
        //skip token for error recovery.
        getNextToken();
    }
}

//top ::= definition | external | expression | ';'
static void MainLoop(){
    while(true){
        fprintf(stderr, "ready> ");
        switch(CurToken){
            case token_eof : return;
            case ';' : getNextToken(); break;//ignore top-level semicolons.
            case token_def : HandleDefinition(); break;
            case token_extern : HandleExtern(); break;
            default : HandleTopLevelExpression(); break;
        }
    }
}

/*
-----------Main driver code----------
*/
int main(){
    //Install standard binary operators.
    //1 is lowest presedence.
    BinOpPrecedence['<'] = 10;
    BinOpPrecedence['+'] = 20;
    BinOpPrecedence['-'] = 20;
    BinOpPrecedence['*'] = 40;//highest

    //Prime the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    //Make the module, which holds all the code.
    InitializeModule();

    // Run the main "interpreter loop" now.
    MainLoop();

    return 0;
}