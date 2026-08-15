#pragma once

#include <cstddef>
#include <functional>

namespace Volt::Runtime{

void applyThreadBudget(int requestedWorkers);

int threadBudget();

int bindToNumaNode();

void runInBoundArena(const std::function<void()>& work);

int duckdbThreadBudget();

std::size_t duckdbMemoryLimitBytes();

}
