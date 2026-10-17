// Console tokeniser and strict number parsing.
#include <irrigation_core.h>
#include <unity.h>

using namespace irr;

void setUp() {}
void tearDown() {}

void test_tokenize_splits_and_lowercases() {
  char line[] = "  SET 1\tDry   70  ";
  char* argv[5];
  TEST_ASSERT_EQUAL_UINT8(4, tokenize(line, argv, 5));
  TEST_ASSERT_EQUAL_STRING("set", argv[0]);
  TEST_ASSERT_EQUAL_STRING("1", argv[1]);
  TEST_ASSERT_EQUAL_STRING("dry", argv[2]);
  TEST_ASSERT_EQUAL_STRING("70", argv[3]);
}

void test_tokenize_respects_max_args_and_empty_lines() {
  char line[] = "a b c d e f";
  char* argv[3];
  TEST_ASSERT_EQUAL_UINT8(3, tokenize(line, argv, 3));
  char empty[] = "   ";
  TEST_ASSERT_EQUAL_UINT8(0, tokenize(empty, argv, 3));
}

void test_parse_uint_is_strict() {
  uint32_t v = 0;
  TEST_ASSERT_TRUE(parseUint("3000", 200, 10000, &v));
  TEST_ASSERT_EQUAL_UINT32(3000, v);
  TEST_ASSERT_FALSE(parseUint("199", 200, 10000, &v));
  TEST_ASSERT_FALSE(parseUint("10001", 200, 10000, &v));
  TEST_ASSERT_FALSE(parseUint("12a", 0, 100, &v));
  TEST_ASSERT_FALSE(parseUint("-5", 0, 100, &v));
  TEST_ASSERT_FALSE(parseUint("", 0, 100, &v));
  TEST_ASSERT_TRUE(parseUint("4294967295", 0, 0xFFFFFFFFUL, &v));
  TEST_ASSERT_FALSE(parseUint("4294967296", 0, 0xFFFFFFFFUL, &v));  // overflow
}

void test_parse_zone() {
  uint8_t z = 0;
  TEST_ASSERT_TRUE(parseZone("1", &z));
  TEST_ASSERT_EQUAL_UINT8(0, z);
  TEST_ASSERT_TRUE(parseZone("4", &z));
  TEST_ASSERT_EQUAL_UINT8(3, z);
  TEST_ASSERT_TRUE(parseZone("*", &z));
  TEST_ASSERT_EQUAL_UINT8(kAllZones, z);
  TEST_ASSERT_TRUE(parseZone("all", &z));
  TEST_ASSERT_FALSE(parseZone("0", &z));
  TEST_ASSERT_FALSE(parseZone("5", &z));
  TEST_ASSERT_FALSE(parseZone("dry", &z));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tokenize_splits_and_lowercases);
  RUN_TEST(test_tokenize_respects_max_args_and_empty_lines);
  RUN_TEST(test_parse_uint_is_strict);
  RUN_TEST(test_parse_zone);
  return UNITY_END();
}
