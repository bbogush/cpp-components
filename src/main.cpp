/*  Copyright (C) 2026 cpp-project-template project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "my_component/my_component.h"

#include <iostream>

int main()
{
    const project::my_component::MyComponent component;
    std::cout << "1 + 2 = " << component.add(1, 2) << std::endl;

    return 0;
}
