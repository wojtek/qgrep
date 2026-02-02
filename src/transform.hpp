// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <vector>

#include "project.hpp"

std::vector<char> applyTransform(TransformKind transform, std::vector<char> data);
