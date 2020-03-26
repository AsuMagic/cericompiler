#pragma once

#include "exceptions.hpp"
#include "types.hpp"
#include "util/string_view.hpp"

#include <cstdint>
#include <iosfwd>

struct Variable;

class IfStatement
{
	friend class CodeGen;

	private:
	std::size_t saved_tag;
};

class WhileStatement
{
	friend class CodeGen;

	private:
	std::size_t saved_tag;
};

class ForStatement
{
	friend class CodeGen;

	private:
	std::size_t     saved_tag;
	const Variable* variable;
};

struct FunctionCall
{
	friend class CodeGen;

	private:
	std::size_t regular_count = 0, float_count = 0;

	public:
	std::string function_name;
	Type        return_type = Type::VOID;
	bool        variadic    = false;
};

class CodeGen
{
	public:
	CodeGen(std::ostream& output) : m_output{output} {}

	void begin_program();
	void finalize_program();

	void begin_executable_section();
	void finalize_executable_section();

	void begin_main_procedure();
	void finalize_main_procedure();

	void begin_global_data_section();
	void finalize_global_data_section();

	void define_global_variable(const Variable& variable);

	void load_variable(const Variable& variable);
	void load_i64(uint64_t value);

	void store_variable(const Variable& variable);

	void alu_and_bool();
	void alu_or_bool();
	void alu_not_bool();

	void alu_add(Type type);
	void alu_sub(Type type);
	void alu_multiply(Type type);
	void alu_divide(Type type);
	void alu_modulus(Type type);

	void alu_equal(Type type);
	void alu_not_equal(Type type);
	void alu_greater_equal(Type type);
	void alu_lower_equal(Type type);
	void alu_greater(Type type);
	void alu_lower(Type type);

	void convert(Type source, Type destination);

	void statement_if_prepare(IfStatement&);
	void statement_if_post_check(IfStatement&);
	void statement_if_with_else(IfStatement&);
	void statement_if_without_else(IfStatement&);
	void statement_if_finalize(IfStatement&);

	void statement_while_prepare(WhileStatement&);
	void statement_while_post_check(WhileStatement&);
	void statement_while_finalize(WhileStatement&);

	void statement_for_prepare(ForStatement&, const Variable& assignement_variable);
	void statement_for_post_assignment(ForStatement&);
	void statement_for_post_check(ForStatement&);
	void statement_for_finalize(ForStatement&);

	void function_call_prepare(FunctionCall&);
	void function_call_param(FunctionCall&, Type type);
	void function_call_finalize(FunctionCall&);

	void debug_display(Type type);

	private:
	void alu_load_binop(Type type);
	void alu_store_f64();

	void alu_compare(Type type, string_view instruction);

	void        function_call_label_param(FunctionCall&, string_view label);
	string_view function_call_register(FunctionCall&, Type type);

	bool is_function_param_type_regular(Type type) const;
	bool is_function_param_type_float(Type type) const;

	std::size_t m_label_tag = 0;

	FunctionCall m_current_function;

	std::ostream& m_output;
};