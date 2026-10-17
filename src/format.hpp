// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once
#include <string.h>

const char kFileFileHeaderMagic[] = "QGF0";

struct FileFileHeader
{
	char magic[4];

	uint32_t fileCount;
	uint32_t compressedSize;
	uint32_t uncompressedSize;

	uint32_t nameBufferOffset;
	uint32_t nameBufferLength;

	uint32_t pathBufferOffset;
	uint32_t pathBufferLength;
};

struct FileFileEntry
{
	uint32_t nameOffset;
	uint32_t pathOffset;
};

const char kDataFileHeaderMagic[] = "QGD2";
// The physical layout is unchanged; JSONL content semantics are incompatible
// with producers that index standalone Codex startup blocks. Ordinary indexes stay QGD2.
const char kJsonlDataFileHeaderMagic[] = "QGD4";

inline bool isKnownDataFileHeader(const char* magic)
{
	return memcmp(magic, kDataFileHeaderMagic, 4) == 0 || memcmp(magic, kJsonlDataFileHeaderMagic, 4) == 0;
}

struct DataFileHeader
{
	char magic[4];
};

struct DataChunkHeader
{
	uint32_t fileCount;
	uint32_t fileTableSize;

	uint32_t compressedSize;
	uint32_t uncompressedSize;

	uint32_t indexSize;
	uint32_t indexHashIterations;

	uint32_t extraSize;
};

struct DataChunkFileHeader
{
	uint32_t nameOffset;
	uint32_t nameLength;
	uint32_t dataOffset;
	uint32_t dataSize;

	uint32_t startLine;
	uint32_t transform; // TransformKind (legacy files may have 0)

	uint64_t fileSize;
	uint64_t timeStamp;
};
