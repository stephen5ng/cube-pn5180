#include <unity.h>
#include <string.h>
#include <stdio.h>

// Sample test functions to demonstrate the testing framework works
void test_basic_math() {
    TEST_ASSERT_EQUAL(4, 2 + 2);
    TEST_ASSERT_EQUAL(10, 5 * 2);
    TEST_ASSERT_NOT_EQUAL(5, 2 + 2);
}

void test_string_operations() {
    char buffer[50];
    strcpy(buffer, "hello");
    strcat(buffer, " world");
    TEST_ASSERT_EQUAL_STRING("hello world", buffer);
    TEST_ASSERT_EQUAL(11, strlen(buffer));
}

void test_array_operations() {
    int numbers[] = {1, 2, 3, 4, 5};
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += numbers[i];
    }
    TEST_ASSERT_EQUAL(15, sum);
}

// Example test showing how to test a simple function
int add_numbers(int a, int b) {
    return a + b;
}

void test_add_function() {
    TEST_ASSERT_EQUAL(7, add_numbers(3, 4));
    TEST_ASSERT_EQUAL(0, add_numbers(-5, 5));
    TEST_ASSERT_EQUAL(-10, add_numbers(-3, -7));
}

void setUp(void) {
    // Set up code runs before each test
}

void tearDown(void) {
    // Tear down code runs after each test
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_basic_math);
    RUN_TEST(test_string_operations);
    RUN_TEST(test_array_operations);
    RUN_TEST(test_add_function);
    
    return UNITY_END();
}