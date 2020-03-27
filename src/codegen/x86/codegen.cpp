#include "codegen.hpp"

#include "compiler.hpp"
#include "types.hpp"
#include "util/enums.hpp"
#include "variable.hpp"

#include <fmt/core.h>
#include <ostream>

void CodeGen::begin_program() { m_compiler.m_output_stream << "# This code was generated by ceri-compiler\n"; }
void CodeGen::finalize_program() {}

void CodeGen::begin_executable_section() { m_compiler.m_output_stream << ".text\n"; }
void CodeGen::finalize_executable_section() {}

void CodeGen::begin_main_procedure()
{
	m_compiler.m_output_stream << fmt::format(
		".globl {mainfunc}\n"
		"{mainfunc}:\n"
		"\tmovq %rsp, %rbp # Save the position of the top of the stack\n",
		fmt::arg("mainfunc", function_mangle_name("main")));
}

void CodeGen::finalize_main_procedure()
{
	m_compiler.m_output_stream << "\tmovq %rbp, %rsp # Restore the position of the top of the stack\n"
								  "\tret\n";
}

void CodeGen::begin_global_data_section()
{
	m_compiler.m_output_stream << ".data\n"
								  ".align 8\n"
								  "__cc_format_string_llu: .string \"%llu\\n\"\n"
								  "__cc_format_string_c:   .string \"%c\" # No newline; this is intended\n"
								  "__cc_format_string_f:   .string \"%f\\n\"\n";
}

void CodeGen::finalize_global_data_section() {}

void CodeGen::define_global_variable(const Variable& variable)
{
	m_compiler.m_output_stream << fmt::format("{}:\n\t", variable.mangled_name());

	// NOTE: non 64-bit loads are still loaded with 64-bit pushes.
	//       Realistically, this does not matter, though.
	//       This might be problematic when dealing with a C FFI for example however, since we (probably) need to clear
	//       up the upper bits of the registers when we pass small data types.

	switch (variable.type.type)
	{
	case Type::BOOLEAN:
	case Type::CHAR: m_compiler.m_output_stream << ".byte 0"; break;
	case Type::UNSIGNED_INT: m_compiler.m_output_stream << ".quad 0"; break;
	case Type::DOUBLE: m_compiler.m_output_stream << ".double 0.0"; break;
	default:
		// HACK: this is gonna break horribly with >64-bit types
		m_compiler.m_output_stream << ".quad 0";
		// throw UnimplementedError{"Unimplemented global variable type"};
	}

	m_compiler.m_output_stream << fmt::format(" # type: {}\n", type_name(variable.type.type).str());
}

void CodeGen::load_variable(const Variable& variable)
{
	m_compiler.m_output_stream << fmt::format("\tpushq {}(%rip)\n", variable.mangled_name());
}
void CodeGen::load_i64(uint64_t value)
{
	if ((value >> 32) != 0)
	{
		// The value does not fit into an imm32, so we cannot use push directly.
		m_compiler.m_output_stream << fmt::format(
			"\tmovq $0x{:016x}, %rax\n"
			"\tpushq %rax\n",
			value);
	}
	else
	{
		m_compiler.m_output_stream << fmt::format("\tpushq $0x{:016x}\n", value);
	}
}

void CodeGen::load_pointer_to_variable(const Variable& variable)
{
	m_compiler.m_output_stream << fmt::format(
		"\tleaq {}(%rip), %rax\n"
		"\tpushq %rax\n",
		variable.mangled_name());
}

void CodeGen::load_value_from_pointer([[maybe_unused]] Type dereferenced_type)
{
	m_compiler.m_output_stream << "\tpopq %rax\n"
								  "\tpushq (%rax)\n";
}

void CodeGen::store_variable(const Variable& variable)
{
	m_compiler.m_output_stream << fmt::format("\tpopq {}(%rip)\n", variable.mangled_name());
}

void CodeGen::store_value_to_pointer([[maybe_unused]] Type value_type)
{
	m_compiler.m_output_stream << "\tpopq %rax\n"
								  "\tpopq %rbx\n"
								  "\tmovq %rbx, (%rax)\n";
}

void CodeGen::alu_and_bool()
{
	alu_load_binop(Type::BOOLEAN);

	m_compiler.m_output_stream << "\tandq %rax, %rbx\n"
								  "\tpushq %rax\n";
}

void CodeGen::alu_or_bool()
{
	alu_load_binop(Type::BOOLEAN);

	m_compiler.m_output_stream << "\torq %rbx, %rax\n"
								  "\tpushq %rax\n";
}

void CodeGen::alu_not_bool() { m_compiler.m_output_stream << "\tnotq (%rsp)\n"; }

void CodeGen::alu_add(Type type)
{
	alu_load_binop(type);

	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		m_compiler.m_output_stream << "\taddq %rbx, %rax\n"
									  "\tpushq %rax\n";
		break;
	}

	case Type::DOUBLE:
	{
		m_compiler.m_output_stream << "\tfaddp %st(0), %st(1)\n";
		alu_store_f64();
		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}
}

void CodeGen::alu_sub(Type type)
{
	alu_load_binop(type);

	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		m_compiler.m_output_stream << "\tsubq %rbx, %rax\n"
									  "\tpushq %rax\n";
		break;
	}

	case Type::DOUBLE:
	{
		m_compiler.m_output_stream << "\tfsubp %st(0), %st(1)\n";
		alu_store_f64();
		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}
}

void CodeGen::alu_multiply(Type type)
{
	alu_load_binop(type);

	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		m_compiler.m_output_stream << "\tmulq %rbx\n"
									  "\tpushq %rax\n";
		break;
	}

	case Type::DOUBLE:
	{
		m_compiler.m_output_stream << "\tfmulp %st(0), %st(1)\n";
		alu_store_f64();
		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}
}

void CodeGen::alu_divide(Type type)
{
	alu_load_binop(type);

	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		m_compiler.m_output_stream << "\tmovq $0, %rdx # Higher part of numerator\n"
									  "\tdiv %rbx # Quotient goes to %rax\n"
									  "\tpushq %rax\n";
		break;
	}

	case Type::DOUBLE:
	{
		m_compiler.m_output_stream << "\tfdivp %st(0), %st(1)\n";
		alu_store_f64();
		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}
}

void CodeGen::alu_modulus(Type type)
{
	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		alu_load_binop(type);
		m_compiler.m_output_stream << "\tmovq $0, %rdx # Higher part of numerator\n"
									  "\tdiv %rbx # Remainder goes to %rdx\n"
									  "\tpushq %rax\n";
		break;
	}

	case Type::DOUBLE:
	{
		FunctionCall call;
		call.variadic      = false;
		call.return_type   = Type::DOUBLE;
		call.function_name = "fmod";

		function_call_prepare(call);
		function_call_param(call, Type::DOUBLE);
		function_call_param(call, Type::DOUBLE);

		m_compiler.m_output_stream
			<< "\t# HACK: swap float operands for modulus '%', as they are pushed the opposite way\n"
			   "\tpxor %xmm0, %xmm1\n"
			   "\tpxor %xmm1, %xmm0\n"
			   "\tpxor %xmm0, %xmm1\n";

		function_call_finalize(call);

		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}
}

void CodeGen::alu_equal(Type type) { alu_compare(type, "je"); }
void CodeGen::alu_not_equal(Type type) { alu_compare(type, "jne"); }
void CodeGen::alu_greater_equal(Type type) { alu_compare(type, "jae"); }
void CodeGen::alu_lower_equal(Type type) { alu_compare(type, "jbe"); }
void CodeGen::alu_greater(Type type) { alu_compare(type, "ja"); }
void CodeGen::alu_lower(Type type) { alu_compare(type, "jb"); }

void CodeGen::convert(Type source, Type destination)
{
	if (source == destination)
	{
		// no-op
		return;
	}

	if (check_enum_range(source, Type::FIRST_INTEGRAL, Type::LAST_INTEGRAL) || destination == Type::CHAR)
	{
		if (check_enum_range(destination, Type::FIRST_INTEGRAL, Type::LAST_INTEGRAL) || destination == Type::CHAR)
		{
			// no-op possible. those are 64-bit values regardless
			return;
		}

		if (destination == Type::DOUBLE)
		{
			m_compiler.m_output_stream << "\tfildq (%rsp)\n"
										  "\tfstpl (%rsp)\n";

			return;
		}
	}

	if (check_enum_range(source, Type::FIRST_FLOATING, Type::LAST_FLOATING))
	{
		if (check_enum_range(destination, Type::FIRST_INTEGRAL, Type::LAST_INTEGRAL) || destination == Type::CHAR)
		{
			m_compiler.m_output_stream << "\tfldl (%rsp)\n"
										  "\tfistpq (%rsp)\n";

			return;
		}
	}

	throw UnimplementedTypeSupportError{fmt::format(
		"unsupported type conversion occured: {} -> {}", type_name(source).str(), type_name(destination).str())};
}

void CodeGen::statement_if_prepare(IfStatement& statement) { statement.saved_tag = ++m_label_tag; }

void CodeGen::statement_if_post_check(IfStatement& statement)
{
	m_compiler.m_output_stream << fmt::format(
		"\tpopq %rax\n"
		"\ttest %rax, %rax\n"
		"\tjz __false{tag}\n"
		"__true{tag}:\n",
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_if_with_else(IfStatement& statement)
{
	m_compiler.m_output_stream << fmt::format(
		"\tjmp __next{tag}\n"
		"__false{tag}:\n",
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_if_without_else(IfStatement& statement)
{
	m_compiler.m_output_stream << fmt::format("__false{}:\n", statement.saved_tag);
}

void CodeGen::statement_if_finalize(IfStatement& statement)
{
	m_compiler.m_output_stream << fmt::format("__next{}:\n", statement.saved_tag);
}

void CodeGen::statement_while_prepare(WhileStatement& statement)
{
	statement.saved_tag = ++m_label_tag;
	m_compiler.m_output_stream << fmt::format("__while{}:\n", statement.saved_tag);
}

void CodeGen::statement_while_post_check(WhileStatement& statement)
{
	m_compiler.m_output_stream << fmt::format(
		"\tpopq %rax\n"
		"\ttest %rax, %rax\n"
		"\tjz __next{tag}\n",
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_while_finalize(WhileStatement& statement)
{
	m_compiler.m_output_stream << fmt::format(
		"\tjmp __while{tag}\n"
		"__next{tag}:\n",
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_for_prepare(ForStatement& statement, const Variable& assignement_variable)
{
	statement.variable  = &assignement_variable;
	statement.saved_tag = ++m_label_tag;
}

void CodeGen::statement_for_post_assignment(ForStatement& statement)
{
	m_compiler.m_output_stream << fmt::format("__for{}:\n", statement.saved_tag);
}

void CodeGen::statement_for_post_check(ForStatement& statement)
{
	// we branch *out* if var < %rax, mind the op order in at&t
	m_compiler.m_output_stream << fmt::format(
		"\tpopq %rax\n"
		"\tcmpq {name}(%rip), %rax\n"
		"\tjl __next{tag}\n",
		fmt::arg("name", statement.variable->mangled_name()),
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::statement_for_finalize(ForStatement& statement)
{
	m_compiler.m_output_stream << fmt::format(
		"\taddq $1, {name}(%rip)\n"
		"\tjmp __for{tag}\n"
		"__next{tag}:\n",
		fmt::arg("name", statement.variable->mangled_name()),
		fmt::arg("tag", statement.saved_tag));
}

void CodeGen::function_call_prepare([[maybe_unused]] FunctionCall& call) {}

void CodeGen::function_call_param(FunctionCall& call, Type type)
{
	if (type == Type::BOOLEAN)
	{
		// Be careful to handle this first: bool is considered regular but we handle it specially anyway!
		m_compiler.m_output_stream << fmt::format(
			"\tpopq {paramreg}\n"
			"\tandq $1, {paramreg}\n",
			fmt::arg("paramreg", function_call_register(call, type).str()));
		++call.regular_count;
	}
	else if (is_function_param_type_regular(type))
	{
		m_compiler.m_output_stream << fmt::format(
			"\tpopq {paramreg}\n", fmt::arg("paramreg", function_call_register(call, type).str()));
		++call.regular_count;
	}
	else if (is_function_param_type_float(type))
	{
		m_compiler.m_output_stream << fmt::format(
			"\tmovsd (%rsp), {paramreg}\n"
			"\taddq $8, %rsp # Effectively pop the float from the stack.\n",
			fmt::arg("paramreg", function_call_register(call, type).str()));
		++call.float_count;
	}
	else
	{
		throw UnimplementedTypeSupportError{"Unimplemented function_call_param() for this type"};
	}
}

void CodeGen::function_call_finalize(FunctionCall& call)
{
	if (call.variadic)
	{
		m_compiler.m_output_stream << fmt::format(
			"\tmovb ${floatcount}, %al\n", fmt::arg("floatcount", call.float_count));
	}

	align_stack();
	m_compiler.m_output_stream << fmt::format(
		"\tcall {function}\n", fmt::arg("function", function_mangle_name(call.function_name)));
	unalign_stack();

	if (call.return_type == Type::BOOLEAN)
	{
		throw UnimplementedTypeSupportError{"Unimplemented return type"};
	}
	else if (is_function_param_type_regular(call.return_type))
	{
		m_compiler.m_output_stream << "\tpushq %rax\n";
	}
	else if (is_function_param_type_float(call.return_type))
	{
		m_compiler.m_output_stream << "\taddq $-8, %rsp\n"
									  "\tmovsd %xmm0, (%rsp)\n";
	}
	else if (call.return_type != Type::VOID)
	{
		throw UnimplementedTypeSupportError{"Unimplemented return type"};
	}
}

void CodeGen::debug_display(Type type)
{
	FunctionCall call;
	call.variadic      = true;
	call.function_name = "printf";
	function_call_prepare(call);

	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		function_call_label_param(call, "__cc_format_string_llu");
		break;
	}

	case Type::BOOLEAN:
	{
		function_call_label_param(call, "__cc_format_string_llu");
		break;
	}

	case Type::CHAR:
	{
		function_call_label_param(call, "__cc_format_string_c");
		break;
	}

	case Type::DOUBLE:
	{
		function_call_label_param(call, "__cc_format_string_f");
		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}

	function_call_param(call, type);
	function_call_finalize(call);
}

long CodeGen::offset_from_frame_pointer() const { return -8; }
long CodeGen::compute_alignment_correction() const { return -(-offset_from_frame_pointer() % 16); }

void CodeGen::align_stack()
{
	const long alignment_correction = compute_alignment_correction();
	if (alignment_correction != 0)
	{
		m_compiler.m_output_stream << fmt::format("\taddq ${}, %rsp # align stack\n", alignment_correction);
	}
}

void CodeGen::unalign_stack()
{
	const long alignment_correction = compute_alignment_correction();
	if (alignment_correction != 0)
	{
		m_compiler.m_output_stream << fmt::format("\taddq ${}, %rsp # unalign stack\n", -alignment_correction);
	}
}

void CodeGen::alu_load_binop(Type type)
{
	switch (type)
	{
	case Type::UNSIGNED_INT:
	case Type::BOOLEAN:
	{
		m_compiler.m_output_stream << "\tpopq %rbx\n"
									  "\tpopq %rax\n";

		break;
	}

	case Type::DOUBLE:
	{
		m_compiler.m_output_stream << "\tfldl (%rsp)\n"
									  "\tfldl 8(%rsp)\n"
									  "\taddq $16, %rsp\n";
		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}
}

void CodeGen::alu_store_f64()
{
	m_compiler.m_output_stream << "\taddq $-8, %rsp\n"
								  "\tfstpl (%rsp)\n";
}

void CodeGen::alu_compare(Type type, string_view instruction)
{
	alu_load_binop(type);

	switch (type)
	{
	case Type::UNSIGNED_INT:
	{
		m_compiler.m_output_stream << "\tcmpq %rbx, %rax\n";
		break;
	}

	case Type::DOUBLE:
	{
		m_compiler.m_output_stream << "\tfcomip\n"
									  "\tfstp %st(0) # Clear fp stack\n";
		break;
	}

	default:
	{
		throw UnimplementedTypeSupportError{};
	}
	}

	++m_label_tag;

	m_compiler.m_output_stream << fmt::format(
		"\t{jumpinstruction} __true{tag}\n"
		"\tpushq $0x0 # No branching: push false\n"
		"\tjmp __next{tag}\n"
		"__true{tag}:\n"
		"\tpushq $0xFFFFFFFFFFFFFFFF\n"
		"__next{tag}:\n",
		fmt::arg("jumpinstruction", instruction.str()),
		fmt::arg("tag", m_label_tag));
}

void CodeGen::function_call_label_param(FunctionCall& call, string_view label)
{
	// HACK: type passed to function_call_register should be a pointer or something
	m_compiler.m_output_stream << fmt::format(
		"\tleaq {label}(%rip), {paramreg}\n",
		fmt::arg("label", label.str()),
		fmt::arg("paramreg", function_call_register(call, Type::UNSIGNED_INT).str()));

	++call.regular_count;
}

string_view CodeGen::function_call_register(FunctionCall& call, Type type)
{
	if (is_function_param_type_regular(type))
	{
		switch (call.regular_count)
		{
		case 0: return "%rdi";
		case 1: return "%rsi";
		case 2: return "%rdx";
		case 3: return "%rcx";
		case 4: return "%r8";
		case 5: return "%r9";
		default: break;
		}
	}
	else if (is_function_param_type_float(type))
	{
		switch (call.float_count)
		{
		case 0: return "%xmm0";
		case 1: return "%xmm1";
		case 2: return "%xmm2";
		case 3: return "%xmm3";
		case 4: return "%xmm4";
		case 5: return "%xmm5";
		case 6: return "%xmm6";
		case 7: return "%xmm7";
		default: break;
		}
	}
	else
	{
		throw UnimplementedTypeSupportError{"Unimplemented function_call_register() for this type"};
	}

	throw UnimplementedError{"Pushing extra parameters to stack not supported yet"};
}

std::string CodeGen::function_mangle_name(string_view name) const
{
	switch (m_compiler.m_config.target)
	{
	case Target::LINUX: return name;
	case Target::APPLE_DARWIN: return fmt::format("_{}", name.str());
	default: throw UnimplementedError{"Unsupported target for function calls"};
	}
}

bool CodeGen::is_function_param_type_regular(Type type) const
{
	return check_enum_range(type, Type::FIRST_INTEGRAL, Type::LAST_INTEGRAL) || type == Type::CHAR
		|| type == Type::BOOLEAN;
}

bool CodeGen::is_function_param_type_float(Type type) const
{
	return check_enum_range(type, Type::FIRST_FLOATING, Type::LAST_FLOATING);
}
