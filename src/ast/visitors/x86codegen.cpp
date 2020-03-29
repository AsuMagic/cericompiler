#include "x86codegen.hpp"

#include <fmt/core.h>

namespace ast
{
namespace visitors
{
void CodeGenX86::operator()(nodes::BinaryExpression& expression)
{
	expression.lhs->visit(*this);
	expression.rhs->visit(*this);
}

} // namespace visitors
} // namespace ast
