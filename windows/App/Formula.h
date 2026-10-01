// Boolean algebra for the class, and formula -> circuit: the Mac app's
// BooleanAlgebra.swift and FormulaCircuit.swift in C++.
//
// The simplest sum of products and product of sums for a truth table
// (Quine-McCluskey, then the smallest cover), reading a formula someone types
// ("F = A'B + AC", "S = A xor B xor Cin", "F(A,B,C) = Σm(1,3,5) + d(7)") into
// a truth table, and planning a circuit for it: switches for the variables,
// gates for the formula (any kind, only NAND or only NOR, up to 4 inputs or
// only 2), a light for each output, laid out left to right in columns.
//
// Minterm numbers follow the truth table: the first variable is the most
// significant bit, so row r of the table is minterm r.

#ifndef CL_WINDOWS_FORMULA_H
#define CL_WINDOWS_FORMULA_H

#include <map>
#include <string>
#include <vector>

namespace formula {

// ---- Minimizing ----

// A product of some of the variables: `mask` bits are the ones left out (a
// dash in the tabular method), `value` the rest.
struct Implicant {
	int value = 0, mask = 0;
	bool covers(int m) const { return (m & ~mask) == value; }
	int literals(int n) const;
	bool operator==(const Implicant& o) const { return value == o.value && mask == o.mask; }
	bool operator<(const Implicant& o) const { return value != o.value ? value < o.value : mask < o.mask; }
};

std::vector<Implicant> minimize(int n, const std::vector<int>& ones, const std::vector<int>& dontCares);

struct Literal { int variable; bool negated; };

// A simplified formula: a sum of products or a product of sums, or a constant.
struct TwoLevel {
	bool sumOfProducts = true;
	std::vector<std::vector<Literal>> terms;
	int constant = -1;   // -1 none, else 0 or 1
	// As text, with ' for NOT: "A'B + AC", "(A + B)(A' + C)".
	std::string text(const std::vector<std::string>& names) const;
};

// The simplest form of a function given as each minterm's value: 1, 0, or -1
// for either (a don't-care).
TwoLevel simplest(bool sumOfProducts, int n, const std::vector<int>& values);

// ---- Reading formulas ----

struct Expr {
	enum Kind { Var, Const, Not, And, Or, Xor } kind = Const;
	int var = 0;
	bool value = false;
	std::vector<Expr> kids;
	bool eval(int m, int n) const;
};

// One output: its name, and a formula or a list of minterms.
struct Function {
	std::string name;
	bool hasExpr = false;
	Expr expr;
	std::vector<int> values;   // per minterm: 1, 0, -1 (don't care)
};

struct Parsed {
	std::vector<std::string> variables;
	std::vector<Function> functions;
};

const int kMaxVariables = 8;

// "F = A'B + C" on each line (or separated by ";"). False with a reason the
// student can act on.
bool parse(const std::string& source, Parsed& out, std::string& error);

// ---- Formula -> circuit ----

enum Style { AnyGates = 0, NandOnly = 1, NorOnly = 2 };
enum Shape { AsWritten = 0, SumOfProducts = 1, ProductOfSums = 2 };

struct Plan {
	struct Part { std::string gate; double x, y; std::string label; };
	struct Wire { int from; std::string fromPin; int to; std::string toPin; };
	std::vector<Part> parts;
	std::vector<Wire> wires;
	int logicGates = 0;
	std::map<std::string, int> gateCounts;   // "NAND" -> 5
	std::string summary() const;
};

Plan plan(const Parsed& formulas, Shape shape, Style style, bool twoInputOnly);

}  // namespace formula

#endif  // CL_WINDOWS_FORMULA_H
