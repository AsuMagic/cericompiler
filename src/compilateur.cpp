//  A compiler from a very simple Pascal-like structured language LL(k)
//  to 64-bit 80x86 Assembly langage
//  Copyright (C) 2019 Pierre Jourlin
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "compilateur.hpp"
#include "tokeniser.hpp"

#include <FlexLexer.h>

#include <array>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

using std::cerr;
using std::cout;
using std::endl;
using std::string;

using namespace std::string_literals;

TOKEN current; // Current token

FlexLexer* lexer = new yyFlexLexer; // This is the flex tokeniser
// tokens can be read using lexer->yylex()
// lexer->yylex() returns the type of the lexicon entry (see enum TOKEN in tokeniser.h)
// and lexer->YYText() returns the lexicon entry as a string

std::set<string> DeclaredVariables;
unsigned long    TagNumber = 0;

bool IsDeclared(const char* id) { return DeclaredVariables.find(id) != DeclaredVariables.end(); }

void PrintErrorPreamble() { cerr << "source:" << lexer->lineno() << ": "; }

void Error(const char* s)
{
	PrintErrorPreamble();
	cerr << "error: " << s << '\n';

	PrintErrorPreamble();
	cerr << "note: when reading token '" << lexer->YYText() << "'\n";
	exit(-1);
}

void TypeCheck(Type a, Type b)
{
	if (a != b)
	{
		Error(("incompatible types: "s + name(a) + ", " + name(b)).c_str());
	}
}

TOKEN read_token() { return (current = TOKEN(lexer->yylex())); }

Type Identifier()
{
	cout << "\tpush " << lexer->YYText() << endl;
	read_token();

	return Type::UNSIGNED_INT;
}

Type Number()
{
	cout << "\tpush $" << atoi(lexer->YYText()) << endl;
	read_token();

	return Type::UNSIGNED_INT;
}

Type Factor()
{
	if (current == RPARENT)
	{
		read_token();
		Type type = Expression();
		if (current != LPARENT)
			Error("expected ')'");
		else
			read_token();

		return type;
	}

	if (current == NUMBER)
	{
		return Number();
	}

	if (current == ID)
	{
		return Identifier();
	}

	Error("expected '(', number or identifier");
}

OPMUL MultiplicativeOperator()
{
	OPMUL opmul;
	if (strcmp(lexer->YYText(), "*") == 0)
		opmul = MUL;
	else if (strcmp(lexer->YYText(), "/") == 0)
		opmul = DIV;
	else if (strcmp(lexer->YYText(), "%") == 0)
		opmul = MOD;
	else if (strcmp(lexer->YYText(), "&&") == 0)
		opmul = AND;
	else
		opmul = WTFM;
	read_token();
	return opmul;
}

Type Term()
{
	OPMUL      mulop;
	const Type first_type = Factor();
	while (current == MULOP)
	{
		mulop               = MultiplicativeOperator(); // Save operator in local variable
		const Type nth_type = Factor();

		TypeCheck(first_type, nth_type);

		cout << "\tpop %rbx" << endl; // get first operand
		cout << "\tpop %rax" << endl; // get second operand
		switch (mulop)
		{
		case AND:
			cout << "\tmulq	%rbx" << endl;        // a * b -> %rdx:%rax
			cout << "\tpush %rax\t# AND" << endl; // store result
			break;
		case MUL:
			cout << "\tmulq	%rbx" << endl;        // a * b -> %rdx:%rax
			cout << "\tpush %rax\t# MUL" << endl; // store result
			break;
		case DIV:
			cout << "\tmovq $0, %rdx" << endl;    // Higher part of numerator
			cout << "\tdiv %rbx" << endl;         // quotient goes to %rax
			cout << "\tpush %rax\t# DIV" << endl; // store result
			break;
		case MOD:
			cout << "\tmovq $0, %rdx" << endl;    // Higher part of numerator
			cout << "\tdiv %rbx" << endl;         // remainder goes to %rdx
			cout << "\tpush %rdx\t# MOD" << endl; // store result
			break;
		case WTFM:
		default: Error("unknown multiplicative operator");
		}
	}

	return first_type;
}

OPADD AdditiveOperator()
{
	OPADD opadd;
	if (strcmp(lexer->YYText(), "+") == 0)
		opadd = ADD;
	else if (strcmp(lexer->YYText(), "-") == 0)
		opadd = SUB;
	else if (strcmp(lexer->YYText(), "||") == 0)
		opadd = OR;
	else
		opadd = WTFA;
	read_token();
	return opadd;
}

Type SimpleExpression()
{
	OPADD      adop;
	const Type first_type = Term();
	while (current == ADDOP)
	{
		adop                = AdditiveOperator(); // Save operator in local variable
		const Type nth_type = Term();

		TypeCheck(first_type, nth_type);

		cout << "\tpop %rbx" << endl; // get first operand
		cout << "\tpop %rax" << endl; // get second operand
		switch (adop)
		{
		case OR:
			cout << "\taddq	%rbx, %rax\t# OR" << endl; // operand1 OR operand2
			break;
		case ADD:
			cout << "\taddq	%rbx, %rax\t# ADD" << endl; // add both operands
			break;
		case SUB:
			cout << "\tsubq	%rbx, %rax\t# SUB" << endl; // substract both operands
			break;
		case WTFA:
		default: Error("unknown additive operator");
		}
		cout << "\tpush %rax" << endl; // store result
	}

	return first_type;
}

void DeclarationPart()
{
	if (current != RBRACKET)
		Error("expected '[' to begin variable declaration block");
	cout << "\t.data" << endl;
	cout << "\t.align 8" << endl;

	read_token();
	if (current != ID)
		Error("expected an identifier");
	cout << lexer->YYText() << ":\t.quad 0" << endl;
	DeclaredVariables.insert(lexer->YYText());
	read_token();
	while (current == COMMA)
	{
		read_token();
		if (current != ID)
			Error("expected an identifier");
		cout << lexer->YYText() << ":\t.quad 0" << endl;
		DeclaredVariables.insert(lexer->YYText());
		read_token();
	}
	if (current != LBRACKET)
		Error("expected ']' to end variable declaration block");
	read_token();
}

OPREL RelationalOperator()
{
	OPREL oprel;
	if (strcmp(lexer->YYText(), "==") == 0)
		oprel = EQU;
	else if (strcmp(lexer->YYText(), "!=") == 0)
		oprel = DIFF;
	else if (strcmp(lexer->YYText(), "<") == 0)
		oprel = INF;
	else if (strcmp(lexer->YYText(), ">") == 0)
		oprel = SUP;
	else if (strcmp(lexer->YYText(), "<=") == 0)
		oprel = INFE;
	else if (strcmp(lexer->YYText(), ">=") == 0)
		oprel = SUPE;
	else
		oprel = WTFR;
	read_token();
	return oprel;
}

Type Expression()
{
	OPREL      oprel;
	const Type first_type = SimpleExpression();
	if (current == RELOP)
	{
		oprel               = RelationalOperator();
		const Type nth_type = SimpleExpression();

		TypeCheck(first_type, nth_type);

		cout << "\tpop %rax" << endl;
		cout << "\tpop %rbx" << endl;
		cout << "\tcmpq %rax, %rbx" << endl;
		switch (oprel)
		{
		case EQU: cout << "\tje Vrai" << ++TagNumber << "\t# If equal" << endl; break;
		case DIFF: cout << "\tjne Vrai" << ++TagNumber << "\t# If different" << endl; break;
		case SUPE: cout << "\tjae Vrai" << ++TagNumber << "\t# If above or equal" << endl; break;
		case INFE: cout << "\tjbe Vrai" << ++TagNumber << "\t# If below or equal" << endl; break;
		case INF: cout << "\tjb Vrai" << ++TagNumber << "\t# If below" << endl; break;
		case SUP: cout << "\tja Vrai" << ++TagNumber << "\t# If above" << endl; break;
		case WTFR:
		default: Error("unknown comparison operator");
		}
		cout << "\tpush $0\t\t# False" << endl;
		cout << "\tjmp Suite" << TagNumber << endl;
		cout << "Vrai" << TagNumber << ":\tpush $0xFFFFFFFFFFFFFFFF\t\t# True" << endl;
		cout << "Suite" << TagNumber << ":" << endl;

		return Type::BOOLEAN;
	}

	return first_type;
}

VariableAssignment AssignementStatement()
{
	string variable;
	if (current != ID)
		Error("expected an identifier");
	if (!IsDeclared(lexer->YYText()))
	{
		cerr << "Variable '" << lexer->YYText() << "' not found" << endl;
		exit(-1);
	}
	variable = lexer->YYText();
	read_token();
	if (current != ASSIGN)
		Error("expected ':=' in variable assignment");
	read_token();
	Type type = Expression();
	cout << "\tpop " << variable << endl;

	return {variable.c_str(), type};
}

void IfStatement()
{
	current = TOKEN(lexer->yylex());
	TypeCheck(Expression(), Type::BOOLEAN);

	const auto tag = ++TagNumber;

	cout << "\tpopq %rax\n";
	cout << "\ttest %rax, %rax\n";
	cout << "\tjz IfFalse" << tag << "\n";
	cout << "IfTrue" << tag << ":\n";

	if (current != KEYWORD || strcmp(lexer->YYText(), "THEN"))
	{
		Error("expected 'THEN' after conditional expression of 'IF' statement");
	}

	current = TOKEN(lexer->yylex());
	Statement();

	if (current == KEYWORD && strcmp(lexer->YYText(), "ELSE") == 0)
	{
		cout << "\tjmp Suite" << tag << '\n';
		cout << "IfFalse" << tag << ":\n";
		current = TOKEN(lexer->yylex());
		Statement();
	}
	else
	{
		cout << "IfFalse" << tag << ":\n";
	}

	cout << "Suite" << tag << ":\n";
}

void WhileStatement()
{
	const auto tag = ++TagNumber;

	current = TOKEN(lexer->yylex());
	TypeCheck(Expression(), Type::BOOLEAN);

	cout << "WhileBegin" << tag << ":\n";
	cout << "\tpop %rax\n";
	cout << "\ttest $0, %rax\n";
	cout << "\tjz Suite" << tag << '\n';

	if (current != KEYWORD || strcmp(lexer->YYText(), "DO"))
	{
		Error("expected 'DO' after conditional expression of 'WHILE' statement");
	}

	current = TOKEN(lexer->yylex());
	Statement();

	cout << "\tjmp WhileBegin" << tag << '\n';
	cout << "\tSuite" << tag << ":\n";
}

void ForStatement()
{
	current = TOKEN(lexer->yylex());
	AssignementStatement();

	if (current != KEYWORD || strcmp(lexer->YYText(), "TO") != 0)
	{
		Error("TO attendu après l'assignation du FOR");
	}

	current = TOKEN(lexer->yylex());
	Expression();

	if (current != KEYWORD || strcmp(lexer->YYText(), "DO") != 0)
	{
		Error("DO attendu après expression du FOR");
	}

	current = TOKEN(lexer->yylex());
	Statement();

	Error("unimplemented codegen for ForStatement");
}

void BlockStatement()
{
	do
	{
		current = TOKEN(lexer->yylex());
		Statement();
	} while (current == SEMICOLON);

	if (current != KEYWORD || strcmp(lexer->YYText(), "END") != 0)
	{
		Error("expected 'END' to finish block statement");
	}

	current = TOKEN(lexer->yylex());
}

void Statement()
{
	if (strcmp(lexer->YYText(), "IF") == 0)
	{
		IfStatement();
	}
	else if (strcmp(lexer->YYText(), "WHILE") == 0)
	{
		WhileStatement();
	}
	else if (strcmp(lexer->YYText(), "FOR") == 0)
	{
		ForStatement();
	}
	else if (strcmp(lexer->YYText(), "BEGIN") == 0)
	{
		BlockStatement();
	}
	else
	{
		AssignementStatement();
	}
}

void StatementPart()
{
	cout << "\t.text\t\t# The following lines contain the program" << endl;
	cout << "\t.globl main\t# The main function must be visible from outside" << endl;
	cout << "main:\t\t\t# The main function body :" << endl;
	cout << "\tmovq %rsp, %rbp\t# Save the position of the stack's top" << endl;
	Statement();
	while (current == SEMICOLON)
	{
		read_token();
		Statement();
	}
	if (current != DOT)
		Error("expected '.' (did you forget a ';'?)");
	read_token();
}

void Program()
{
	if (current == RBRACKET)
		DeclarationPart();
	StatementPart();
}

int main()
{ // First version : Source code on standard input and assembly code on standard output
	// Header for gcc assembler / linker
	cout << "\t\t\t# This code was produced by the CERI Compiler" << endl;
	// Let's proceed to the analysis and code production
	read_token();
	Program();
	// Trailer for the gcc assembler / linker
	cout << "\tmovq %rbp, %rsp\t\t# Restore the position of the stack's top" << endl;
	cout << "\tret\t\t\t# Return from main function" << endl;
	if (current != FEOF)
	{
		cerr << "extraneous characters at end of file: [" << current << "]";
		Error("."); // unexpected characters at the end of program
	}
}