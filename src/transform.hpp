// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <vector>
#include <string>

#include "project.hpp"

std::vector<char> applyTransform(TransformKind transform, std::vector<char> data);

// Feed decoded UTF-8 JSONL without retaining the complete decoded transcript.
// Holds only the pending record and the existing transform's selected output.
class JsonlTransformStream
{
public:
	void append(const char* data, size_t size);
	std::vector<char> finish();

private:
	std::string pending;
	std::string output;
	bool skipLF = false;
};
