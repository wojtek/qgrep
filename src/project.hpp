// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <string>
#include <vector>
#include <memory>

class Output;
class Regex;

enum TransformKind
{
	TRANSFORM_NONE = 0,
	// 1/3 are the legacy JSONL identities, without completed Codex messages.
	// The project header also versions the content contract: incompatible
	// producers require an explicit canonical rebuild, never update migration.
	TRANSFORM_JSONL = 4,
	TRANSFORM_GEMINI = 2,
	// Same JSONL selection, decoded from Zstd. Existing file metadata records
	// this identity so an old undecoded/empty entry is never reused as current.
	TRANSFORM_JSONL_ZSTD = 5
};

std::string getProjectPath(const char* name);
std::string getProjectName(const char* path);

std::vector<std::string> getProjects();
std::vector<std::string> getProjectPaths(const char* list);

struct ProjectGroup
{
	ProjectGroup* parent;

	std::vector<std::string> paths;
	std::vector<std::string> files;
	std::shared_ptr<Regex> include;
	std::shared_ptr<Regex> exclude;
	TransformKind transform;
	bool transformSet;

	std::vector<std::unique_ptr<ProjectGroup>> groups;
};

std::unique_ptr<ProjectGroup> parseProject(Output* output, const char* file);
const char* getProjectDataFileMagic(ProjectGroup* group);
// Throws on an incompatible existing index; never repairs or replaces it.
void requireCompatibleProjectIndex(Output* output, const char* path, bool allowMissing = false);
bool isFileAcceptable(ProjectGroup* group, const char* path);

struct FileInfo
{
	std::string path;
	uint64_t timeStamp;
	uint64_t fileSize;
	TransformKind transform;
};

std::vector<FileInfo> getProjectGroupFiles(Output* output, ProjectGroup* group);
