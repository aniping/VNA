#pragma once

#include <gtest/gtest.h>

#define VNA_REQUIRE(expression) \
    ASSERT_TRUE((expression))
