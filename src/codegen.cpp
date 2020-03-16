#include "codegen.hpp"

#include "compiler.hpp"
#include "types.hpp"
#include "util/enums.hpp"

#include <fmt/core.h>
#include <ostream>

void CodeGen::begin_program() { m_output << "# This code was generated by ceri-compiler\n"; }
void CodeGen::finalize_program() {}

void CodeGen::begin_executable_section() { m_output << ".text\n"; }
void CodeGen::finalize_executable_section() {}

void CodeGen::begin_main_procedure()
{
	m_output << ".globl main\n"
				"main:\n"
				"\tmovq %rsp, %rbp # Save the position of the top of the stack\n";
}

void CodeGen::finalize_main_procedure()
{
	m_output << "\t movq %rbp, %rsp # Restore the position of the top of the stack\n"
				"\tret\n";
}

void CodeGen::begin_global_data_section()
{
	m_output << ".data\n"
				".align 8\n"
				"__cc_format_string_llu: .string \"%llu\\n\"\n";
}

void CodeGen::finalize_global_data_section() {}

void CodeGen::define_global_variable(const Variable& variable)
{
	m_output << fmt::format("{}:\t.quad 0 # type: {}\n", variable.name, type_name(variable.type.type).str());
}

void CodeGen::load_variable(const Variable& variable) { m_output << fmt::format("\tpush {}\n", variable.name); }
void CodeGen::load_i64(int64_t value) { m_output << fmt::format("\tpush ${}\n", value); }

void CodeGen::store_variable(const Variable& variable) { m_output << fmt::format("\tpop {}\n", variable.name); }

void CodeGen::alu_and_bool()
{
	alu_load_binop_i64();

	m_output << "\tandq %rax, %rbx\n"
				"\tpush %rax\n";
}

void CodeGen::alu_or_bool()
{
	alu_load_binop_i64();

	m_output << "\torq %rbx, %rax\n"
				"\tpush %rax\n";
}

void CodeGen::alu_add_i64()
{
	alu_load_binop_i64();

	m_output << "\taddq %rbx, %rax\n"
				"\tpush %rax\n";
}

void CodeGen::alu_sub_i64()
{
	alu_load_binop_i64();

	m_output << "\tsubq %rbx, %rax\n"
				"\tpush %rax\n";
}

void CodeGen::alu_multiply_i64()
{
	alu_load_binop_i64();

	m_output << "\tmulq %rbx\n"
				"\tpush %rax\n";
}

void CodeGen::alu_divide_i64()
{
	alu_load_binop_i64();

	m_output << "\tmovq $0, %rdx # Higher part of numerator\n"
				"\tdiv %rbx # Quotient goes to %rax\n"
				"\tpush %rax\n";
}

void CodeGen::alu_modulus_i64()
{
	alu_load_binop_i64();

	m_output << "\tmovq $0, %rdx # Higher part of numerator\n"
				"\tdiv %rbx # Remainder goes to %rdx\n"
				"\tpush %rax\n";
}

void CodeGen::alu_equal_i64() { alu_compare_i64("je"); }
void CodeGen::alu_not_equal_i64() { alu_compare_i64("jne"); }
void CodeGen::alu_greater_equal_i64() { alu_compare_i64("jae"); }
void CodeGen::alu_lower_equal_i64() { alu_compare_i64("jbe"); }
void CodeGen::alu_greater_i64() { alu_compare_i64("ja"); }
void CodeGen::alu_lower_i64() { alu_compare_i64("je"); }

bool CodeGen::convert(Type source, Type destination)
{
	if (source == destination)
	{
		// no-op
		return true;
	}

	if (check_enum_range(source, Type::FIRST_INTEGRAL, Type::LAST_INTEGRAL) || destination == Type::CHAR)
	{
		if (check_enum_range(destination, Type::FIRST_INTEGRAL, Type::LAST_INTEGRAL) || destination == Type::CHAR)
		{
			// no-op possible. those are 64-bit values regardless
			return true;
		}
	}

	return false;
}

IfStatement CodeGen::statement_if_prepare() { return {++m_label_tag}; }

void CodeGen::statement_if_post_check(IfStatement statement)
{
	m_output << fmt::format(
		"\tpopq %rax\n"
		"\ttest %rax, %rax\n"
		"\tjz __false{tag}\n"
		"__true{tag}:\n",
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_if_with_else(IfStatement statement)
{
	m_output << fmt::format(
		"\tjmp __next{tag}\n"
		"__false{tag}:\n",
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_if_without_else(IfStatement statement)
{
	m_output << fmt::format("__false{}:\n", statement.saved_tag);
}

void CodeGen::statement_if_finalize(IfStatement statement)
{
	m_output << fmt::format("__next{}:\n", statement.saved_tag);
}

WhileStatement CodeGen::statement_while_prepare()
{
	WhileStatement statement{++m_label_tag};
	m_output << fmt::format("__while{}:\n", statement.saved_tag);

	return statement;
}

void CodeGen::statement_while_post_check(WhileStatement statement)
{
	m_output << fmt::format(
		"\tpop %rax\n"
		"\ttest %rax, %rax\n"
		"\tjz __next{tag}\n",
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_while_finalize(WhileStatement statement)
{
	m_output << fmt::format(
		"\tjmp __while{tag}\n"
		"__next{tag}:\n",
		fmt::arg("tag", statement.saved_tag));
}

ForStatement CodeGen::statement_for_prepare(const Variable& assignement_variable)
{
	return {++m_label_tag, &assignement_variable};
}

void CodeGen::statement_for_post_assignment(ForStatement statement)
{
	m_output << fmt::format("__for{}:\n", statement.saved_tag);
}

void CodeGen::statement_for_post_check(ForStatement statement)
{
	// we branch *out* if var < %rax, mind the op order in at&t
	m_output << fmt::format(
		"\tpop %rax\n"
		"\tcmpq {name}, %rax\n"
		"\tjl __next{tag}\n",
		fmt::arg("name", statement.variable->name),
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_for_finalize(ForStatement statement)
{
	m_output << fmt::format(
		"\taddq $1, {name}\n"
		"\tjmp __for{tag}\n"
		"__next{tag}:\n",
		fmt::arg("name", statement.variable->name),
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::debug_display_i64()
{
	m_output << "\tmovq $__cc_format_string_llu, %rdi\n"
				"\tpop %rsi\n";

	debug_call_printf();
}

void CodeGen::alu_load_binop_i64()
{
	m_output << "\tpop %rbx\n"
				"\tpop %rax\n";
}

void CodeGen::alu_compare_i64(string_view instruction)
{
	alu_load_binop_i64();

	++m_label_tag;

	m_output << fmt::format(
		"\tcmpq %rbx, %rax\n"
		"\t{jumpinstruction} __true{tag}\n"
		"\tpush $0x0 # No branching: push false\n"
		"\tjmp __next{tag}\n"
		"__true{tag}:\n"
		"\tpush $0xFFFFFFFFFFFFFFFF\n"
		"__next{tag}:\n",
		fmt::arg("jumpinstruction", instruction.str()),
		fmt::arg("tag", m_label_tag));
}

void CodeGen::debug_call_printf()
{
	m_output << "\tmovb $0, %al # printf is variadic, as per the ABI we write the number of float parameters\n"
				"\t call printf\n";
}
