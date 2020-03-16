#include "codegen.hpp"

#include "compiler.hpp"
#include "types.hpp"

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
	m_output << variable.name << ":\t.quad 0 # type: " << type_name(variable.type.type) << '\n';
}

void CodeGen::load_variable(const Variable& variable) { m_output << "\tpush " << variable.name << '\n'; }
void CodeGen::load_i64(int64_t value) { m_output << "\tpush $" << value << '\n'; }
