#include "my_component/my_component.h"

#include <gtest/gtest.h>

TEST(MyComponentTest, Add)
{
    project::my_component::MyComponent component;
    EXPECT_EQ(component.add(1, 2), 3);
}
