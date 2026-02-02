// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#include "transform.hpp"

#include <string>
#include <vector>

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

static bool parseContentItemObject(JsonCursor& c, std::vector<JsonPiece>& pieces)
{
	if (!consume(c, '{')) return false;

	std::string key;
	std::string type;
	std::string text;
	std::string thinking;

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
		}
		else if (key == "thinking")
		{
			if (!parseJsonString(c, thinking)) return false;
		}
		else
		{
			if (!skipValue(c)) return false;
		}

		skipWhitespace(c);

		if (consume(c, '}')) break;
		if (!consume(c, ',')) return false;
	}

	if (type == "text")
	{
		if (!text.empty())
			pieces.push_back({ false, std::move(text) });
	}
	else if (type == "thinking")
	{
		if (!thinking.empty())
			pieces.push_back({ true, std::move(thinking) });
	}

	return true;
}

static bool parseContentArray(JsonCursor& c, std::vector<JsonPiece>& pieces)
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
			if (!parseContentItemObject(c, pieces)) return false;
		}
		else if (*c.p == '"')
		{
			std::string text;
			if (!parseJsonString(c, text)) return false;
			if (!text.empty())
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

static bool parseContentValue(JsonCursor& c, std::vector<JsonPiece>& pieces)
{
	skipWhitespace(c);
	if (c.p >= c.end) return false;

	if (*c.p == '"')
	{
		std::string text;
		if (!parseJsonString(c, text)) return false;
		if (!text.empty())
			pieces.push_back({ false, std::move(text) });
		return true;
	}

	if (*c.p == '[')
		return parseContentArray(c, pieces);

	return skipValue(c);
}

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
			if (!parseMessageObject(c, role, pieces)) return false;
			hasMessage = true;
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
} // namespace

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

	return data;
}
