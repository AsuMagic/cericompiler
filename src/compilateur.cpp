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
#include "codegen.hpp"
#include "tokeniser.hpp"

#include <FlexLexer.h>

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using std::cerr;
using std::cout;
using std::string;

using namespace std::string_literals;

TOKEN current; // Current token

FlexLexer* lexer = new yyFlexLexer; // This is the flex tokeniser
// tokens can be read using lexer->yylex()
// lexer->yylex() returns the type of the lexicon entry (see enum TOKEN in tokeniser.h)
// and lexer->YYText() returns the lexicon entry as a string

std::unordered_map<std::string, VariableType> DeclaredVariables;
unsigned long                             TagNumber = 0;

bool is_declared(const char* id) { return DeclaredVariables.find(id) != DeclaredVariables.end(); }

void print_error_preamble() { cerr << "source:" << lexer->lineno() << ": "; }

void error(const char* s)
{
	print_error_preamble();
	cerr << "error: " << s << '\n';

	print_error_preamble();
	cerr << "note: when reading token '" << lexer->YYText() << "'\n";
	exit(-1);
}

void check_type(Type a, Type b)
{
	assert(int(a) < int(Type::CONCEPT_BEGIN) && "Only the second operand of TypeCheck may be a type concept");

	bool match = true;

	if (b == Type::ARITHMETIC)
	{
		if (a != Type::UNSIGNED_INT)
		{
			match = false;
		}
	}
	else if (a != b)
	{
		match = false;
	}

	if (!match)
	{
		error(("incompatible types: "s + type_name(a) + ", " + type_name(b)).c_str());
	}
}

TOKEN read_token() { return (current = TOKEN(lexer->yylex())); }

bool match_keyword(const char* keyword) { return (current == KEYWORD && std::strcmp(lexer->YYText(), keyword) == 0); }

Type parse_identifier()
{
	const std::string name = lexer->YYText();

	const auto it = DeclaredVariables.find(name);
	if (it == DeclaredVariables.end())
	{
		error((std::string("use of undeclared identifier '") + name + '\'').c_str());
	}

	const VariableType& variable = it->second;

	cout << "\tpush " << lexer->YYText() << '\n';
	read_token();

	return variable.type;
}

Type parse_number()
{
	cout << "\tpush $" << atoi(lexer->YYText()) << '\n';
	read_token();

	return Type::UNSIGNED_INT;
}

Type parse_factor()
{
	if (current == RPARENT)
	{
		read_token();
		Type type = parse_expression();
		if (current != LPARENT)
			error("expected ')'");
		else
			read_token();

		return type;
	}

	if (current == NUMBER)
	{
		return parse_number();
	}

	if (current == ID)
	{
		return parse_identifier();
	}

	error("expected '(', number or identifier");
}

MultiplicativeOperator parse_multiplicative_operator()
{
	MultiplicativeOperator opmul;
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

Type parse_term()
{
	MultiplicativeOperator      mulop;
	const Type first_type = parse_factor();
	while (current == MULOP)
	{
		mulop               = parse_multiplicative_operator(); // Save operator in local variable
		const Type nth_type = parse_factor();

		check_type(first_type, nth_type);

		cout << "\tpop %rbx\n"; // get first operand
		cout << "\tpop %rax\n"; // get second operand
		switch (mulop)
		{
		case AND:
			check_type(first_type, Type::BOOLEAN);
			cout << "\tmulq	%rbx\n";        // a * b -> %rdx:%rax
			cout << "\tpush %rax\t# AND\n"; // store result
			break;
		case MUL:
			check_type(first_type, Type::ARITHMETIC);
			cout << "\tmulq	%rbx\n";        // a * b -> %rdx:%rax
			cout << "\tpush %rax\t# MUL\n"; // store result
			break;
		case DIV:
			check_type(first_type, Type::ARITHMETIC);
			cout << "\tmovq $0, %rdx\n";    // Higher part of numerator
			cout << "\tdiv %rbx\n";         // quotient goes to %rax
			cout << "\tpush %rax\t# DIV\n"; // store result
			break;
		case MOD:
			check_type(first_type, Type::ARITHMETIC);
			cout << "\tmovq $0, %rdx\n";    // Higher part of numerator
			cout << "\tdiv %rbx\n";         // remainder goes to %rdx
			cout << "\tpush %rdx\t# MOD\n"; // store result
			break;
		case WTFM:
		default: error("unknown multiplicative operator");
		}
	}

	return first_type;
}

AdditiveOperator parse_additive_operator()
{
	AdditiveOperator opadd;
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

Type parse_simple_expression()
{
	AdditiveOperator      adop;
	const Type first_type = parse_term();
	while (current == ADDOP)
	{
		adop                = parse_additive_operator(); // Save operator in local variable
		const Type nth_type = parse_term();

		check_type(first_type, nth_type);

		cout << "\tpop %rbx\n"; // get first operand
		cout << "\tpop %rax\n"; // get second operand
		switch (adop)
		{
		case OR:
			check_type(first_type, Type::BOOLEAN);
			cout << "\taddq	%rbx, %rax\t# OR\n"; // operand1 OR operand2
			break;
		case ADD:
			check_type(first_type, Type::ARITHMETIC);
			cout << "\taddq	%rbx, %rax\t# ADD\n"; // add both operands
			break;
		case SUB:
			check_type(first_type, Type::ARITHMETIC);
			cout << "\tsubq	%rbx, %rax\t# SUB\n"; // substract both operands
			break;
		case WTFA:
		default: error("unknown additive operator");
		}
		cout << "\tpush %rax\n"; // store result
	}

	return first_type;
}

void parse_declaration_block()
{
	if (!match_keyword("VAR"))
	{
		return;
	}

	do
	{
		std::vector<std::string> current_declarations;

		do
		{
			read_token();
			current_declarations.push_back(lexer->YYText());
			read_token();
		} while (current == COMMA);

		if (current != COLON)
		{
			error("expected ':' after variable name list in declaration block");
		}

		read_token();

		const Type type = parse_type();

		for (auto& name : current_declarations)
		{
			DeclaredVariables.emplace(std::move(name), VariableType{type});
		}

		read_token();
	} while (current == SEMICOLON);

	if (current != DOT)
	{
		error("expected '.' at end of declaration block");
	}

	read_token();
}

Type parse_type()
{
	if (current != TYPE)
	{
		error("expected type");
	}

	const char* name = lexer->YYText();

	if (std::strcmp(name, "INTEGER") == 0)
	{
		return Type::UNSIGNED_INT;
	}

	if (std::strcmp(name, "BOOLEAN") == 0)
	{
		return Type::BOOLEAN;
	}

	error("unrecognized type; this is a compiler bug");
}

RelationalOperator parse_relational_operator()
{
	RelationalOperator oprel = RelationalOperator::WTFR;
	if (strcmp(lexer->YYText(), "==") == 0)
		oprel = RelationalOperator::EQU;
	else if (strcmp(lexer->YYText(), "!=") == 0)
		oprel = RelationalOperator::DIFF;
	else if (strcmp(lexer->YYText(), "<") == 0)
		oprel = RelationalOperator::INF;
	else if (strcmp(lexer->YYText(), ">") == 0)
		oprel = RelationalOperator::SUP;
	else if (strcmp(lexer->YYText(), "<=") == 0)
		oprel = RelationalOperator::INFE;
	else if (strcmp(lexer->YYText(), ">=") == 0)
		oprel = RelationalOperator::SUPE;

	read_token();
	return oprel;
}

Type parse_expression()
{
	RelationalOperator      oprel;
	const Type first_type = parse_simple_expression();
	if (current == RELOP)
	{
		oprel               = parse_relational_operator();
		const Type nth_type = parse_simple_expression();

		check_type(first_type, nth_type);

		cout << "\tpop %rax\n";
		cout << "\tpop %rbx\n";
		cout << "\tcmpq %rax, %rbx\n";
		switch (oprel)
		{
		case RelationalOperator::EQU: cout << "\tje Vrai" << ++TagNumber << "\t# If equal\n"; break;
		case RelationalOperator::DIFF: cout << "\tjne Vrai" << ++TagNumber << "\t# If different\n"; break;
		case RelationalOperator::SUPE: cout << "\tjae Vrai" << ++TagNumber << "\t# If above or equal\n"; break;
		case RelationalOperator::INFE: cout << "\tjbe Vrai" << ++TagNumber << "\t# If below or equal\n"; break;
		case RelationalOperator::INF: cout << "\tjb Vrai" << ++TagNumber << "\t# If below\n"; break;
		case RelationalOperator::SUP: cout << "\tja Vrai" << ++TagNumber << "\t# If above\n"; break;
		case RelationalOperator::WTFR:
		default: error("unknown comparison operator");
		}
		cout << "\tpush $0\t\t# False\n";
		cout << "\tjmp Suite" << TagNumber << '\n';
		cout << "Vrai" << TagNumber << ":\tpush $0xFFFFFFFFFFFFFFFF\t\t# True\n";
		cout << "Suite" << TagNumber << ":\n";

		return Type::BOOLEAN;
	}

	return first_type;
}

VariableAssignment parse_assignment_statement()
{
	if (current != ID)
		error("expected an identifier");

	const std::string name = lexer->YYText();
	const auto it = DeclaredVariables.find(name);

	if (it == DeclaredVariables.end())
	{
		error((std::string("variable '") + name + "' not found").c_str());
	}

	const VariableType& variable = it->second;

	read_token();
	if (current != ASSIGN)
		error("expected ':=' in variable assignment");
	read_token();
	Type type = parse_expression();
	cout << "\tpop " << name << '\n';

	check_type(type, variable.type);

	return {name, type};
}

void parse_if_statement()
{
	read_token();
	check_type(parse_expression(), Type::BOOLEAN);

	const auto tag = ++TagNumber;

	cout << "\tpopq %rax\n";
	cout << "\ttest %rax, %rax\n";
	cout << "\tjz IfFalse" << tag << '\n';
	cout << "IfTrue" << tag << ":\n";

	if (current != KEYWORD || strcmp(lexer->YYText(), "THEN"))
	{
		error("expected 'THEN' after conditional expression of 'IF' statement");
	}

	read_token();
	parse_statement();

	if (current == KEYWORD && strcmp(lexer->YYText(), "ELSE") == 0)
	{
		cout << "\tjmp Suite" << tag << '\n';
		cout << "IfFalse" << tag << ":\n";
		read_token();
		parse_statement();
	}
	else
	{
		cout << "IfFalse" << tag << ":\n";
	}

	cout << "Suite" << tag << ":\n";
}

void parse_while_statement()
{
	const auto tag = ++TagNumber;

	cout << "WhileBegin" << tag << ":\n";

	read_token();
	const Type type = parse_expression();
	check_type(type, Type::BOOLEAN);

	cout << "\tpop %rax\n";
	cout << "\ttest %rax, %rax\n";
	cout << "\tjz Suite" << tag << '\n';

	if (current != KEYWORD || strcmp(lexer->YYText(), "DO"))
	{
		error("expected 'DO' after conditional expression of 'WHILE' statement");
	}

	read_token();
	parse_statement();

	cout << "\tjmp WhileBegin" << tag << '\n';
	cout << "Suite" << tag << ":\n";
}

void parse_for_statement()
{
	const auto tag = ++TagNumber;

	read_token();
	const auto assignment = parse_assignment_statement();
	check_type(assignment.type.type, Type::UNSIGNED_INT);

	if (current != KEYWORD || strcmp(lexer->YYText(), "TO") != 0)
	{
		error("expected 'TO' after assignement in 'FOR' statement");
	}

	cout << "ForBegin" << tag << ":\n";

	read_token();
	check_type(parse_expression(), Type::UNSIGNED_INT);

	cout << "\tpop %rax\n";
	// we branch *out* if var < %rax, mind the op order in at&t
	cout << "\tcmpq " << assignment.name << ", %rax\n";
	cout << "\tjl Suite" << tag << '\n';

	if (current != KEYWORD || strcmp(lexer->YYText(), "DO") != 0)
	{
		error("expected 'DO' after max expression in 'FOR' statement");
	}

	read_token();
	parse_statement();

	cout << "\taddq $1, " << assignment.name << '\n';
	cout << "\tjmp ForBegin" << tag << '\n';
	cout << "Suite" << tag << ":\n";
}

void parse_block_statement()
{
	do
	{
		read_token();
		parse_statement();
	} while (current == SEMICOLON);

	if (current != KEYWORD || strcmp(lexer->YYText(), "END") != 0)
	{
		error("expected 'END' to finish block statement");
	}

	read_token();
}

void parse_display_statement()
{
	read_token();
	const Type type = parse_expression();

	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		cout << "\tmovq $__cc_format_string, %rdi\n";
		break;
	}

	default:
	{
		error("unimplemented DISPLAY statement for this type, sorry");
	}
	}

	cout << "\tpop %rsi # Value to be displayed\n";
	cout << "\tmovb $0, %al # Number of floating point parameters (varargs)\n";
	cout << "\tcall printf\n";
}

void parse_statement()
{
	if (strcmp(lexer->YYText(), "IF") == 0)
	{
		parse_if_statement();
	}
	else if (strcmp(lexer->YYText(), "WHILE") == 0)
	{
		parse_while_statement();
	}
	else if (strcmp(lexer->YYText(), "FOR") == 0)
	{
		parse_for_statement();
	}
	else if (strcmp(lexer->YYText(), "BEGIN") == 0)
	{
		parse_block_statement();
	}
	else if (strcmp(lexer->YYText(), "DISPLAY") == 0)
	{
		parse_display_statement();
	}
	else
	{
		parse_assignment_statement();
	}
}

void parse_statement_part()
{
	emit_main_preamble();

	parse_statement();
	while (current == SEMICOLON)
	{
		read_token();
		parse_statement();
	}
	if (current != DOT)
		error("expected '.' (did you forget a ';'?)");
	read_token();
}

void parse_program()
{
	cout << "\t.data\n";
	cout << "\t.align 8\n";
	cout << "__cc_format_string: .string \"%llu\\n\"\n";

	parse_declaration_block();

	for (const auto& it : DeclaredVariables)
	{
		const auto& name = it.first;
		const VariableType& variable = it.second;

		cout << name << ":\t.quad 0 # type: " << type_name(variable.type) << '\n';
	}

	parse_statement_part();
}

int main()
{
	// Read the source from stdin, output the assembly to stdout

	cout << "# This code was produced by the CERI Compiler\n";

	// Let's proceed to the analysis and code production
	read_token();
	parse_program();
	// Trailer for the gcc assembler / linker
	cout << "\tmovq %rbp, %rsp\t\t# Restore the position of the stack's top\n";
	cout << "\tret\t\t\t# Return from main function\n";
	if (current != FEOF)
	{
		// FIXME: this is not printing the right stuff
		error((string("extraneous characters at end of file: [") + std::to_string(current) + "]").c_str());
	}
}
