#pragma once
// Tiny single-header test framework, no dependencies.
//
//   TEST(name) { CHECK(x); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); }
//   int main() { return RUN_ALL_TESTS(); }
//
// RUN_ALL_TESTS() prints "PASS name" or "FAIL name (file:line: expr)" per failed check, a summary line,
// and returns the number of failed tests (0 when everything passed).

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& Registry() {
  static std::vector<Case> r;
  return r;
}
inline int& CurrentFailures() {
  static int n = 0;
  return n;
}
inline const char*& CurrentName() {
  static const char* n = "";
  return n;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline void Fail(const char* file, int line, const std::string& expr) {
  CurrentFailures()++;
  std::printf("FAIL %s (%s:%d: %s)\n", CurrentName(), file, line, expr.c_str());
}

// A function rather than an `if` in the macro, so a CHECK on a compile-time constant does not warn (C4127).
inline bool Check(bool ok, const char* file, int line, const char* expr) {
  if (!ok) Fail(file, line, expr);
  return ok;
}

template <class T>
std::string Str(const T& v) {
  std::ostringstream o;
  o.precision(9);
  o << v;
  return o.str();
}

template <class A, class B>
bool CheckEq(const char* file, int line, const char* ea, const char* eb, const A& a, const B& b) {
  if (a == b) return true;
  Fail(file, line, std::string(ea) + " == " + eb + "  [" + Str(a) + " vs " + Str(b) + "]");
  return false;
}

template <class A, class B, class E>
bool CheckNear(const char* file, int line, const char* ea, const char* eb, const A& a, const B& b, const E& eps) {
  double d = std::fabs((double)a - (double)b);
  if (d <= (double)eps) return true;
  Fail(file, line, std::string(ea) + " ~= " + eb + "  [" + Str(a) + " vs " + Str(b) + ", diff " + Str(d) + "]");
  return false;
}

inline int RunAll() {
  int failedTests = 0;
  for (const Case& c : Registry()) {
    CurrentName() = c.name;
    CurrentFailures() = 0;
    c.fn();
    if (CurrentFailures() == 0)
      std::printf("PASS %s\n", c.name);
    else
      failedTests++;
  }
  int total = (int)Registry().size();
  std::printf("\n%d tests: %d passed, %d failed\n", total, total - failedTests, failedTests);
  return failedTests;
}

}  // namespace test

#define TEST(name)                                                   \
  static void test_fn_##name();                                      \
  static ::test::Registrar test_reg_##name(#name, &test_fn_##name); \
  static void test_fn_##name()

#define CHECK(cond) ::test::Check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)

#define CHECK_EQ(a, b) ::test::CheckEq(__FILE__, __LINE__, #a, #b, (a), (b))
#define CHECK_NEAR(a, b, eps) ::test::CheckNear(__FILE__, __LINE__, #a, #b, (a), (b), (eps))
#define RUN_ALL_TESTS() ::test::RunAll()
