// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#include "transform.hpp"

#include <string>
#include <vector>
#include <algorithm>

#include <stdint.h>
#include <string.h>

namespace
{

struct JsonCursor
{
	const char* p;
	const char* end;
};

static bool isWhitespace(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void skipWhitespace(JsonCursor& c)
{
	while (c.p < c.end && isWhitespace(*c.p))
		c.p++;
}

static bool consume(JsonCursor& c, char ch)
{
	if (c.p < c.end && *c.p == ch)
	{
		c.p++;
		return true;
	}

	return false;
}

static bool parseHex4(const char* p, uint32_t& out)
{
	out = 0;
	for (int i = 0; i < 4; ++i)
	{
		char ch = p[i];
		uint32_t v = 0;

		if (ch >= '0' && ch <= '9') v = ch - '0';
		else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
		else return false;

		out = (out << 4) | v;
	}

	return true;
}

static void appendCodepointUtf8(std::string& out, uint32_t cp)
{
	if (cp <= 0x7f)
	{
		out.push_back(static_cast<char>(cp));
	}
	else if (cp <= 0x7ff)
	{
		out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
	}
	else if (cp <= 0xffff)
	{
		out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
	}
	else if (cp <= 0x10ffff)
	{
		out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
	}
	else
	{
		out.push_back('?');
	}
}

static bool parseJsonString(JsonCursor& c, std::string& out)
{
	if (!consume(c, '"')) return false;

	out.clear();

	while (c.p < c.end)
	{
		char ch = *c.p++;

		if (ch == '"')
			return true;

		if (ch == '\\')
		{
			if (c.p >= c.end) return false;

			char esc = *c.p++;
			switch (esc)
			{
			case '"': out.push_back('"'); break;
			case '\\': out.push_back('\\'); break;
			case '/': out.push_back('/'); break;
			case 'b': out.push_back('\b'); break;
			case 'f': out.push_back('\f'); break;
			case 'n': out.push_back('\n'); break;
			case 'r': out.push_back('\n'); break;
			case 't': out.push_back('\t'); break;
			case 'u':
				if (c.end - c.p < 4) return false;
				{
					uint32_t cp = 0;
					if (!parseHex4(c.p, cp)) return false;
					c.p += 4;

					if (cp >= 0xd800 && cp <= 0xdbff)
					{
						if (c.end - c.p >= 6 && c.p[0] == '\\' && c.p[1] == 'u')
						{
							uint32_t cp2 = 0;
							if (parseHex4(c.p + 2, cp2) && cp2 >= 0xdc00 && cp2 <= 0xdfff)
							{
								cp = 0x10000 + ((cp - 0xd800) << 10) + (cp2 - 0xdc00);
								c.p += 6;
							}
							else
							{
								cp = 0xfffd;
							}
						}
						else
						{
							cp = 0xfffd;
						}
					}
					else if (cp >= 0xdc00 && cp <= 0xdfff)
					{
						cp = 0xfffd;
					}

					appendCodepointUtf8(out, cp);
				}
				break;

			default:
				return false;
			}

			continue;
		}

		if (static_cast<unsigned char>(ch) < 0x20)
			return false;

		out.push_back(ch);
	}

	return false;
}

static bool skipValue(JsonCursor& c);

static bool skipNumber(JsonCursor& c)
{
	const char* start = c.p;

	if (c.p < c.end && *c.p == '-') c.p++;

	if (c.p < c.end && *c.p == '0')
	{
		c.p++;
	}
	else
	{
		while (c.p < c.end && *c.p >= '0' && *c.p <= '9')
			c.p++;
	}

	if (c.p < c.end && *c.p == '.')
	{
		c.p++;
		while (c.p < c.end && *c.p >= '0' && *c.p <= '9')
			c.p++;
	}

	if (c.p < c.end && (*c.p == 'e' || *c.p == 'E'))
	{
		c.p++;
		if (c.p < c.end && (*c.p == '+' || *c.p == '-')) c.p++;
		while (c.p < c.end && *c.p >= '0' && *c.p <= '9')
			c.p++;
	}

	return c.p > start;
}

static bool skipArray(JsonCursor& c)
{
	if (!consume(c, '[')) return false;

	skipWhitespace(c);
	if (consume(c, ']')) return true;

	for (;;)
	{
		skipWhitespace(c);
		if (!skipValue(c)) return false;
		skipWhitespace(c);

		if (consume(c, ']')) break;
		if (!consume(c, ',')) return false;
	}

	return true;
}

static bool skipObject(JsonCursor& c)
{
	if (!consume(c, '{')) return false;

	skipWhitespace(c);
	if (consume(c, '}')) return true;

	std::string key;

	for (;;)
	{
		skipWhitespace(c);
		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);
		if (!skipValue(c)) return false;
		skipWhitespace(c);

		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	return true;
}

static bool skipValue(JsonCursor& c)
{
	skipWhitespace(c);
	if (c.p >= c.end) return false;

	switch (*c.p)
	{
	case '"':
		{
			std::string tmp;
			return parseJsonString(c, tmp);
		}
	case '{':
		return skipObject(c);
	case '[':
		return skipArray(c);
	case 't':
		if (c.end - c.p >= 4 && memcmp(c.p, "true", 4) == 0)
		{
			c.p += 4;
			return true;
		}
		return false;
	case 'f':
		if (c.end - c.p >= 5 && memcmp(c.p, "false", 5) == 0)
		{
			c.p += 5;
			return true;
		}
		return false;
	case 'n':
		if (c.end - c.p >= 4 && memcmp(c.p, "null", 4) == 0)
		{
			c.p += 4;
			return true;
		}
		return false;
	default:
		if (*c.p == '-' || (*c.p >= '0' && *c.p <= '9'))
			return skipNumber(c);
		return false;
	}
}

struct JsonPiece
{
	bool thinking;
	std::string text;
};

// Key whitelist for tool input/arguments extraction.
// Only these keys are indexed — everything else is skipped to keep the index small.
static bool isIndexableToolKey(const std::string& key)
{
	return key == "file_path"
		|| key == "path"
		|| key == "command"
		|| key == "description"
		|| key == "pattern"
		|| key == "query"
		|| key == "prompt"
		|| key == "skill"
		|| key == "cmd"       // Codex exec_command
		|| key == "workdir"   // Codex exec_command
		|| key == "message";  // Codex spawn_agent
}

// Collect whitelisted string values from a flat JSON object (for tool input/arguments extraction).
// Only extracts keys that pass isIndexableToolKey; skips everything else.
static bool collectShortStringFields(JsonCursor& c, std::vector<std::string>& values)
{
	if (!consume(c, '{')) return false;

	skipWhitespace(c);
	if (consume(c, '}')) return true;

	std::string key;

	for (;;)
	{
		skipWhitespace(c);
		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);

		if (c.p < c.end && *c.p == '"' && isIndexableToolKey(key))
		{
			std::string val;
			if (!parseJsonString(c, val)) return false;
			if (!val.empty())
			{
				// Flatten newlines for single-line searchability
				for (size_t i = 0; i < val.size(); ++i)
					if (val[i] == '\n' || val[i] == '\r')
						val[i] = ' ';
				values.push_back(std::move(val));
			}
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	return true;
}

// Parse a content item object from the content array.
// Handles: text, output_text (Codex), thinking, tool_use (Claude).
static bool parseContentItemObject(JsonCursor& c, std::vector<JsonPiece>& pieces, const char* textType = nullptr)
{
	if (!consume(c, '{')) return false;

	std::string key;
	std::string type;
	std::string text;
	bool hasText = false;
	std::string thinking;
	std::string toolName;
	std::vector<std::string> toolFields;

	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;

		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);

		if (key == "type")
		{
			if (!parseJsonString(c, type)) return false;
		}
		else if (key == "text")
		{
			if (!parseJsonString(c, text)) return false;
			hasText = true;
		}
		else if (key == "thinking")
		{
			if (!parseJsonString(c, thinking)) return false;
		}
		else if (key == "name")
		{
			// Tool name for tool_use content items
			if (c.p < c.end && *c.p == '"')
			{
				if (!parseJsonString(c, toolName)) return false;
			}
			else
			{
				if (!skipValue(c)) return false;
			}
		}
		else if (key == "input")
		{
			// Tool input object for tool_use content items
			if (c.p < c.end && *c.p == '{')
			{
				if (!collectShortStringFields(c, toolFields)) return false;
			}
			else
			{
				if (!skipValue(c)) return false;
			}
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);

		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	if (textType)
	{
		if (type == textType && hasText)
			pieces.push_back({ false, std::move(text) });
	}
	else if (type == "text" || type == "output_text" || type == "input_text")
	{
		if (!text.empty())
			pieces.push_back({ false, std::move(text) });
	}
	else if (type == "thinking")
	{
		if (!thinking.empty())
			pieces.push_back({ true, std::move(thinking) });
	}
	else if (type == "tool_use")
	{
		// Emit tool invocation arguments as searchable text.
		// Format: [tool:ToolName] field1 | field2 | ...
		if (!toolName.empty() && !toolFields.empty())
		{
			std::string line = "[tool:" + toolName + "]";
			for (size_t i = 0; i < toolFields.size(); ++i)
			{
				if (i > 0)
					line += " | ";
				else
					line += " ";
				line += toolFields[i];
			}
			pieces.push_back({ false, std::move(line) });
		}
	}

	return true;
}

static bool parseContentArray(JsonCursor& c, std::vector<JsonPiece>& pieces, const char* textType = nullptr)
{
	if (!consume(c, '[')) return false;

	skipWhitespace(c);
	if (consume(c, ']')) return true;

	for (;;)
	{
		skipWhitespace(c);

		if (c.p >= c.end) return false;

		if (*c.p == '{')
		{
			if (!parseContentItemObject(c, pieces, textType)) return false;
		}
		else if (*c.p == '"')
		{
			std::string text;
			if (!parseJsonString(c, text)) return false;
			if (!textType && !text.empty())
				pieces.push_back({ false, std::move(text) });
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);

		if (consume(c, ']')) break;
		if (!consume(c, ',')) return false;
	}

	return true;
}

static bool parseContentValue(JsonCursor& c, std::vector<JsonPiece>& pieces, const char* textType = nullptr)
{
	skipWhitespace(c);
	if (c.p >= c.end) return false;

	if (*c.p == '"')
	{
		std::string text;
		if (!parseJsonString(c, text)) return false;
		if (!textType && !text.empty())
			pieces.push_back({ false, std::move(text) });
		return true;
	}

	if (*c.p == '[')
		return parseContentArray(c, pieces, textType);

	return skipValue(c);
}

// Parse Claude's message object: {"role": "...", "content": ...}
static bool parseMessageObject(JsonCursor& c, std::string& role, std::vector<JsonPiece>& pieces)
{
	skipWhitespace(c);
	if (c.p >= c.end) return false;

	if (*c.p != '{')
		return skipValue(c);

	consume(c, '{');

	std::string key;

	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;

		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);

		if (key == "role")
		{
			if (!parseJsonString(c, role)) return false;
		}
		else if (key == "content")
		{
			if (!parseContentValue(c, pieces)) return false;
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);

		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	return true;
}

// Completed messages have different content tags from raw response messages.
// Keep their text as one LF-joined piece, matching the visible message owner.
static bool parseCompletedMessage(JsonCursor& c, std::string& role, std::vector<JsonPiece>& pieces)
{
	skipWhitespace(c);
	if (c.p >= c.end) return false;
	if (*c.p != '{') return skipValue(c);
	consume(c, '{');
	std::string key, type, text;
	bool hasText = false;
	JsonCursor content = { nullptr, nullptr };
	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);
		if ((key == "type" || key == "text") && c.p < c.end && *c.p == '"')
		{
			if (!parseJsonString(c, key == "type" ? type : text)) return false;
			if (key == "text") hasText = true;
		}
		else
		{
			const char* start = c.p;
			if (!skipValue(c)) return false;
			if (key == "content") content = { start, c.p };
		}
		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}
	bool user = type == "UserMessage" || type == "userMessage";
	bool assistant = type == "AgentMessage" || type == "agentMessage";
	if (!user && !assistant) return true;
	role = user ? "user" : "assistant";
	if (!assistant || !hasText)
	{
		text.clear();
		std::vector<JsonPiece> parts;
		if (content.p && !parseContentValue(content, parts, user ? "text" : "Text")) return false;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			if (i) text += '\n';
			text += parts[i].text;
		}
	}
	if (!text.empty()) pieces.push_back({ false, std::move(text) });
	return true;
}

// Parse Codex payload object.
// Handles: message (assistant text), function_call (tool invocation),
// user_message, agent_message. Skips function_call_output and reasoning.
static bool parseCodexPayload(JsonCursor& c, std::string& role, std::vector<JsonPiece>& pieces, bool& hasContent)
{
	skipWhitespace(c);
	if (c.p >= c.end) return false;
	if (*c.p != '{') return skipValue(c);

	consume(c, '{');

	std::string key;
	std::string payloadType;
	std::string payloadRole;
	std::string payloadName;
	std::string payloadArguments;
	std::string payloadMessage;
	std::vector<JsonPiece> contentPieces;
	std::string completedRole;
	std::vector<JsonPiece> completedPieces;

	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;

		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);

		if (key == "type")
		{
			if (c.p < c.end && *c.p == '"')
			{
				if (!parseJsonString(c, payloadType)) return false;
			}
			else
			{
				if (!skipValue(c)) return false;
			}
		}
		else if (key == "role")
		{
			if (c.p < c.end && *c.p == '"')
			{
				if (!parseJsonString(c, payloadRole)) return false;
			}
			else
			{
				if (!skipValue(c)) return false;
			}
		}
		else if (key == "name")
		{
			if (c.p < c.end && *c.p == '"')
			{
				if (!parseJsonString(c, payloadName)) return false;
			}
			else
			{
				if (!skipValue(c)) return false;
			}
		}
		else if (key == "arguments")
		{
			// Codex stores arguments as a JSON-encoded string
			if (c.p < c.end && *c.p == '"')
			{
				if (!parseJsonString(c, payloadArguments)) return false;
			}
			else
			{
				if (!skipValue(c)) return false;
			}
		}
		else if (key == "message")
		{
			// user_message/agent_message payload.message is a string
			if (c.p < c.end && *c.p == '"')
			{
				if (!parseJsonString(c, payloadMessage)) return false;
			}
			else
			{
				if (!skipValue(c)) return false;
			}
		}
		else if (key == "item")
		{
			if (!parseCompletedMessage(c, completedRole, completedPieces)) return false;
		}
		else if (key == "content")
		{
			// message payload has content array with output_text items
			if (!parseContentValue(c, contentPieces)) return false;
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	// Dispatch based on Codex payload type
	if (payloadType == "item_completed")
	{
		role = completedRole;
		for (auto& p : completedPieces) pieces.push_back(std::move(p));
		hasContent = !completedRole.empty();
	}
	else if (payloadType == "message")
	{
		// Codex assistant/developer message with content array
		role = payloadRole;
		for (auto& p : contentPieces)
			pieces.push_back(std::move(p));
		hasContent = true;
	}
	else if (payloadType == "function_call")
	{
		// Codex tool invocation: double-parse arguments JSON string
		role = "assistant";

		if (!payloadName.empty())
		{
			std::vector<std::string> argFields;

			if (!payloadArguments.empty())
			{
				JsonCursor ac = { payloadArguments.data(), payloadArguments.data() + payloadArguments.size() };
				skipWhitespace(ac);
				if (ac.p < ac.end && *ac.p == '{')
					collectShortStringFields(ac, argFields);
			}

			if (!argFields.empty())
			{
				std::string line = "[tool:" + payloadName + "]";
				for (size_t i = 0; i < argFields.size(); ++i)
				{
					if (i > 0)
						line += " | ";
					else
						line += " ";
					line += argFields[i];
				}
				pieces.push_back({ false, std::move(line) });
			}
		}

		hasContent = true;
	}
	else if (payloadType == "user_message")
	{
		role = "user";
		if (!payloadMessage.empty())
			pieces.push_back({ false, std::move(payloadMessage) });
		hasContent = true;
	}
	else if (payloadType == "agent_message")
	{
		role = "assistant";
		if (!payloadMessage.empty())
			pieces.push_back({ false, std::move(payloadMessage) });
		hasContent = true;
	}
	// Skip: function_call_output (bulk stdout), reasoning (encrypted), etc.

	return true;
}

static bool parseTopObject(JsonCursor& c, std::string& role, std::vector<JsonPiece>& pieces, bool& hasMessage)
{
	skipWhitespace(c);
	if (!consume(c, '{')) return false;

	hasMessage = false;

	std::string key;

	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;

		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);

		if (key == "message")
		{
			// Claude Code: top-level "message" object with role + content
			if (!parseMessageObject(c, role, pieces)) return false;
			hasMessage = true;
		}
		else if (key == "payload")
		{
			// Codex CLI: top-level "payload" object (function_call, message, etc.)
			if (!parseCodexPayload(c, role, pieces, hasMessage)) return false;
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);

		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	return true;
}

static void stripAnsiInPlace(std::string& s)
{
	size_t esc = s.find('\x1b');
	if (esc == std::string::npos) return;

	std::string out;
	out.reserve(s.size());

	for (size_t i = 0; i < s.size(); )
	{
		unsigned char ch = static_cast<unsigned char>(s[i]);

		if (ch == 0x1b)
		{
			if (i + 1 < s.size() && s[i + 1] == '[')
			{
				i += 2;
				while (i < s.size())
				{
					unsigned char c = static_cast<unsigned char>(s[i]);
					if (c >= 0x40 && c <= 0x7e)
					{
						i++;
						break;
					}
					i++;
				}
			}
			else
			{
				i++;
			}
			continue;
		}

		out.push_back(static_cast<char>(ch));
		i++;
	}

	s.swap(out);
}

static void appendWithNewline(std::string& out, const std::string& text)
{
	if (text.empty()) return;

	out.append(text);
	if (out.empty() || out.back() != '\n')
		out.push_back('\n');
}

static void appendPiece(std::string& out, const JsonPiece& piece, bool allowThinking)
{
	std::string text = piece.text;
	stripAnsiInPlace(text);

	if (text.empty()) return;

	if (piece.thinking && allowThinking)
	{
		std::string marked = "[thinking] ";
		marked += text;
		appendWithNewline(out, marked);
	}
	else if (!piece.thinking)
	{
		appendWithNewline(out, text);
	}
}

static void processJsonlLine(const char* line, size_t length, std::string& out)
{
	if (length == 0) return;

	if (length >= 3 && static_cast<unsigned char>(line[0]) == 0xef &&
		static_cast<unsigned char>(line[1]) == 0xbb &&
		static_cast<unsigned char>(line[2]) == 0xbf)
	{
		line += 3;
		length -= 3;
		if (length == 0) return;
	}

	JsonCursor c = { line, line + length };
	std::string role;
	std::vector<JsonPiece> pieces;
	bool hasMessage = false;

	if (!parseTopObject(c, role, pieces, hasMessage))
		return;

	if (!hasMessage) return;

	bool isUser = (role == "user");
	bool isAssistant = (role == "assistant");

	if (!isUser && !isAssistant) return;

	for (size_t i = 0; i < pieces.size(); ++i)
		appendPiece(out, pieces[i], isAssistant);
}

static std::vector<char> transformJsonl(const char* data, size_t size)
{
	std::string out;
	out.reserve(size);

	const char* line = data;
	const char* end = data + size;

	while (line < end)
	{
		const char* next = static_cast<const char*>(memchr(line, '\n', end - line));
		if (!next)
		{
			processJsonlLine(line, end - line, out);
			break;
		}

		processJsonlLine(line, next - line, out);
		line = next + 1;
	}

	return std::vector<char>(out.begin(), out.end());
}

// --- Gemini session JSON transform ---

// Parse Gemini content field: a string, or an array of strings / {"text":"..."} objects.
static bool parseGeminiContent(JsonCursor& c, std::string& content)
{
	skipWhitespace(c);
	if (c.p >= c.end) return false;

	if (*c.p == '"')
		return parseJsonString(c, content);

	if (*c.p == '[')
	{
		if (!consume(c, '[')) return false;
		skipWhitespace(c);
		if (consume(c, ']')) return true;

		for (;;)
		{
			skipWhitespace(c);
			if (c.p >= c.end) return false;

			if (*c.p == '"')
			{
				std::string text;
				if (!parseJsonString(c, text)) return false;
				if (!text.empty())
				{
					if (!content.empty()) content.push_back('\n');
					content.append(text);
				}
			}
			else if (*c.p == '{')
			{
				consume(c, '{');
				std::string key;
				for (;;)
				{
					skipWhitespace(c);
					if (consume(c, '}')) break;
					if (!parseJsonString(c, key)) return false;
					skipWhitespace(c);
					if (!consume(c, ':')) return false;
					skipWhitespace(c);
					if (key == "text" && c.p < c.end && *c.p == '"')
					{
						std::string text;
						if (!parseJsonString(c, text)) return false;
						if (!text.empty())
						{
							if (!content.empty()) content.push_back('\n');
							content.append(text);
						}
					}
					else
					{
						if (!skipValue(c)) return false;
					}
					skipWhitespace(c);
					if (consume(c, '}')) break;
					if (!consume(c, ',')) return false;
				}
			}
			else
			{
				if (!skipValue(c)) return false;
			}

			skipWhitespace(c);
			if (consume(c, ']')) break;
			if (!consume(c, ',')) return false;
		}
		return true;
	}

	return skipValue(c);
}

// Parse Gemini thoughts array: [{"subject":"...", "description":"...", ...}, ...]
static bool parseGeminiThoughts(JsonCursor& c, std::vector<std::string>& descriptions)
{
	if (!consume(c, '[')) return false;
	skipWhitespace(c);
	if (consume(c, ']')) return true;

	for (;;)
	{
		skipWhitespace(c);
		if (c.p >= c.end) return false;

		if (*c.p == '{')
		{
			consume(c, '{');
			std::string key, desc;
			for (;;)
			{
				skipWhitespace(c);
				if (consume(c, '}')) break;
				if (!parseJsonString(c, key)) return false;
				skipWhitespace(c);
				if (!consume(c, ':')) return false;
				skipWhitespace(c);
				if (key == "description" && c.p < c.end && *c.p == '"')
				{
					if (!parseJsonString(c, desc)) return false;
				}
				else
				{
					if (!skipValue(c)) return false;
				}
				skipWhitespace(c);
				if (consume(c, '}')) break;
				if (!consume(c, ',')) return false;
			}
			if (!desc.empty())
				descriptions.push_back(std::move(desc));
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);
		if (consume(c, ']')) break;
		if (!consume(c, ',')) return false;
	}
	return true;
}

// Parse a single Gemini tool call: {name, args, result(skip), resultDisplay(skip), ...}
static bool parseGeminiToolCall(JsonCursor& c, std::vector<JsonPiece>& pieces)
{
	if (!consume(c, '{')) return false;

	std::string key, toolName;
	std::vector<std::string> argFields;

	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);

		if (key == "name" && c.p < c.end && *c.p == '"')
		{
			if (!parseJsonString(c, toolName)) return false;
		}
		else if (key == "args" && c.p < c.end && *c.p == '{')
		{
			if (!collectShortStringFields(c, argFields)) return false;
		}
		else
		{
			// Skip result, resultDisplay, functionResponse, status, etc.
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	if (!toolName.empty() && !argFields.empty())
	{
		std::string line = "[tool:" + toolName + "]";
		for (size_t i = 0; i < argFields.size(); ++i)
		{
			if (i > 0)
				line += " | ";
			else
				line += " ";
			line += argFields[i];
		}
		pieces.push_back({false, std::move(line)});
	}

	return true;
}

// Parse Gemini toolCalls array
static bool parseGeminiToolCalls(JsonCursor& c, std::vector<JsonPiece>& pieces)
{
	if (!consume(c, '[')) return false;
	skipWhitespace(c);
	if (consume(c, ']')) return true;

	for (;;)
	{
		skipWhitespace(c);
		if (c.p >= c.end) return false;

		if (*c.p == '{')
		{
			if (!parseGeminiToolCall(c, pieces)) return false;
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);
		if (consume(c, ']')) break;
		if (!consume(c, ',')) return false;
	}
	return true;
}

// Parse a single Gemini message and append extracted text to out.
static bool parseGeminiMessage(JsonCursor& c, std::string& out)
{
	if (c.p >= c.end || *c.p != '{')
		return skipValue(c);

	consume(c, '{');

	std::string key, type, content;
	std::vector<std::string> thoughtDescs;
	std::vector<JsonPiece> toolPieces;

	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;

		if (!parseJsonString(c, key)) return false;
		skipWhitespace(c);
		if (!consume(c, ':')) return false;
		skipWhitespace(c);

		if (key == "type" && c.p < c.end && *c.p == '"')
		{
			if (!parseJsonString(c, type)) return false;
		}
		else if (key == "content")
		{
			if (!parseGeminiContent(c, content)) return false;
		}
		else if (key == "thoughts" && c.p < c.end && *c.p == '[')
		{
			if (!parseGeminiThoughts(c, thoughtDescs)) return false;
		}
		else if (key == "toolCalls" && c.p < c.end && *c.p == '[')
		{
			if (!parseGeminiToolCalls(c, toolPieces)) return false;
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	bool isUser = (type == "user");
	bool isGemini = (type == "gemini");
	if (!isUser && !isGemini) return true;

	// Output content
	if (!content.empty())
	{
		std::string text = content;
		stripAnsiInPlace(text);
		appendWithNewline(out, text);
	}

	// Output thoughts (gemini messages only)
	if (isGemini)
	{
		for (auto& desc : thoughtDescs)
		{
			std::string text = desc;
			stripAnsiInPlace(text);
			if (!text.empty())
			{
				std::string marked = "[thinking] " + text;
				appendWithNewline(out, marked);
			}
		}
	}

	// Output tool call info
	for (auto& piece : toolPieces)
		appendPiece(out, piece, isGemini);

	return true;
}

// Parse the messages array from a Gemini session file.
static bool parseGeminiMessagesArray(JsonCursor& c, std::string& out)
{
	if (!consume(c, '[')) return false;
	skipWhitespace(c);
	if (consume(c, ']')) return true;

	for (;;)
	{
		skipWhitespace(c);
		if (!parseGeminiMessage(c, out)) return false;
		skipWhitespace(c);
		if (consume(c, ']')) break;
		if (!consume(c, ',')) return false;
	}
	return true;
}

static std::vector<char> transformGemini(const char* data, size_t size)
{
	std::string out;
	out.reserve(size / 4);

	JsonCursor c = {data, data + size};

	// Skip BOM if present
	if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xef &&
		static_cast<unsigned char>(data[1]) == 0xbb &&
		static_cast<unsigned char>(data[2]) == 0xbf)
	{
		c.p += 3;
	}

	skipWhitespace(c);
	if (c.p >= c.end || *c.p != '{')
		return std::vector<char>(out.begin(), out.end());

	consume(c, '{');

	std::string key;

	for (;;)
	{
		skipWhitespace(c);
		if (consume(c, '}')) break;

		if (!parseJsonString(c, key)) break;
		skipWhitespace(c);
		if (!consume(c, ':')) break;
		skipWhitespace(c);

		if (key == "messages" && c.p < c.end && *c.p == '[')
		{
			parseGeminiMessagesArray(c, out);
			break; // messages is all we need
		}
		else
		{
			if (!skipValue(c)) break;
		}

		skipWhitespace(c);
		if (consume(c, '}')) break;
		if (!consume(c, ',')) break;
	}

	return std::vector<char>(out.begin(), out.end());
}
} // namespace

void JsonlTransformStream::append(const char* data, size_t size)
{
	while (size)
	{
		if (skipLF)
		{
			skipLF = false;
			if (*data == '\n') { ++data; --size; continue; }
		}
		const char* lf = static_cast<const char*>(memchr(data, '\n', size));
		const char* cr = static_cast<const char*>(memchr(data, '\r', size));
		const char* end = lf && cr ? std::min(lf, cr) : lf ? lf : cr;
		if (!end)
		{
			pending.append(data, size);
			break;
		}
		pending.append(data, end - data);
		processJsonlLine(pending.data(), pending.size(), output);
		pending.clear();
		skipLF = *end == '\r';
		size -= end - data + 1;
		data = end + 1;
	}
}

std::vector<char> JsonlTransformStream::finish()
{
	processJsonlLine(pending.data(), pending.size(), output);
	pending.clear();
	return std::vector<char>(output.begin(), output.end());
}

std::vector<char> applyTransform(TransformKind transform, std::vector<char> data)
{
	if (transform == TRANSFORM_NONE)
		return data;

	if (transform == TRANSFORM_JSONL)
	{
		if (data.empty())
			return data;

		return transformJsonl(&data[0], data.size());
	}

	if (transform == TRANSFORM_GEMINI)
	{
		if (data.empty())
			return data;

		return transformGemini(&data[0], data.size());
	}

	return data;
}
