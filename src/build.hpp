// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <memory>
#include <vector>

class Output;
struct DataChunkHeader;
enum TransformKind;

struct BuildContext;
struct FileInfo;

BuildContext* buildStart(Output* output, const char* path, unsigned int fileCount = 0);

void buildAppendFilePart(BuildContext* context, const char* path, unsigned int startLine, const char* data, size_t dataSize, TransformKind transform, uint64_t timeStamp, uint64_t fileSize);
bool buildAppendFile(BuildContext* context, const char* path, uint64_t timeStamp, uint64_t fileSize, TransformKind transform);
bool buildAppendChunk(BuildContext* context, const DataChunkHeader& header, std::unique_ptr<char[]>& compressedData, std::unique_ptr<char[]>& index, std::unique_ptr<char[]>& extra, bool firstFileIsSuffix);

unsigned int buildFinish(BuildContext* context);

bool buildProject(Output* output, const char* path);
void buildReportInputCoverage(Output* output, const std::vector<FileInfo>& files);
