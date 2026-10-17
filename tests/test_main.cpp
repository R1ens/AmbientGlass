#include <windows.h>

#include "test_framework.h"

int main() {
  SetConsoleOutputCP(CP_UTF8);
  return RUN_ALL_TESTS();
}
