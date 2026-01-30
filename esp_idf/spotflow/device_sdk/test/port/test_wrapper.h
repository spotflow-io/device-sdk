#pragma once

#include "unity.h"

#define TEST_SPOTFLOW_ASSERT_TRUE(cond)    TEST_ASSERT_TRUE(cond)
#define TEST_SPOTFLOW_ASSERT_FALSE(cond)   TEST_ASSERT_FALSE(cond)
#define TEST_SPOTFLOW_ASSERT_EQUAL(exp, act) TEST_ASSERT_EQUAL((exp), (act))
#define TEST_SPOTFLOW_ASSERT_EQUAL_UINT8_ARRAY(exp, act, len) TEST_ASSERT_EQUAL_UINT8_ARRAY((exp), (act), (len))
#define TEST_SPOTFLOW_ASSERT_LESS_OR_EQUAL(exp, act) TEST_ASSERT_LESS_OR_EQUAL((exp), (act))
