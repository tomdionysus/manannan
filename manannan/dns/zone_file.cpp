//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/dns/zone_file.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <array>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace manannan::dns {
namespace {

constexpr std::uint32_t max_ttl = 0x7fffffff;  // RFC 2181 8

struct Token {
  std::string text;  // escapes kept as written
  bool quoted = false;
};

// A logical line: one physical line, or several joined by parentheses.
struct Line {
  std::size_t number = 0;
  bool blank_owner = false;  // began with a space or tab
  std::vector<Token> tokens;
};

[[noreturn]] void fail(std::size_t line, const std::string& why) { throw std::invalid_argument("line " + std::to_string(line) + ": " + why); }

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

class Lexer {
 public:
  explicit Lexer(std::string_view text) : _text(text) {}

  bool next(Line& line) {
    while (_pos < _text.size()) {
      line = Line{_line, is_space(_text[_pos]), {}};
      bool open = false;
      while (_pos < _text.size()) {
        const char c = _text[_pos];
        if (c == '\n') {
          ++_line;
          ++_pos;
          if (open) continue;
          break;
        }
        if (is_space(c)) {
          ++_pos;
        } else if (c == ';') {
          while (_pos < _text.size() && _text[_pos] != '\n') ++_pos;
        } else if (c == '(') {
          if (open) fail(_line, "nested parenthesis");
          open = true;
          ++_pos;
        } else if (c == ')') {
          if (!open) fail(_line, "unmatched parenthesis");
          open = false;
          ++_pos;
        } else if (c == '"') {
          ++_pos;
          line.tokens.push_back(Token{read_until_quote(), true});
        } else {
          line.tokens.push_back(Token{read_plain(), false});
        }
      }
      if (open) fail(_line, "unclosed parenthesis");
      if (!line.tokens.empty()) return true;
    }
    return false;
  }

 private:
  // One escape: the backslash and the character after it, which must be on the same line.
  void read_escape(std::string& out) {
    if (_pos + 1 >= _text.size() || _text[_pos + 1] == '\n') fail(_line, "a backslash ends the line");
    out += _text[_pos];
    out += _text[_pos + 1];
    _pos += 2;
  }

  std::string read_until_quote() {
    std::string out;
    while (true) {
      if (_pos >= _text.size() || _text[_pos] == '\n') fail(_line, "unclosed quote");
      const char c = _text[_pos];
      if (c == '"') {
        ++_pos;
        return out;
      }
      if (c == '\\') {
        read_escape(out);
      } else {
        out += c;
        ++_pos;
      }
    }
  }

  std::string read_plain() {
    std::string out;
    while (_pos < _text.size()) {
      const char c = _text[_pos];
      if (is_space(c) || c == '\n' || c == ';' || c == '(' || c == ')' || c == '"') break;
      if (c == '\\') {
        read_escape(out);
      } else {
        out += c;
        ++_pos;
      }
    }
    return out;
  }

  std::string_view _text;
  std::size_t _pos = 0;
  std::size_t _line = 1;
};

// The octets a character-string token stands for: `\DDD` is an octet, `\X` is X.
std::string unescape(const std::string& text, std::size_t line) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\\') {
      out += text[i];
      continue;
    }
    ++i;  // the lexer leaves a character after every backslash
    if (!is_digit(text[i])) {
      out += text[i];
      continue;
    }
    if (i + 2 >= text.size() || !is_digit(text[i + 1]) || !is_digit(text[i + 2])) fail(line, "\\DDD needs three digits");
    const int value = (text[i] - '0') * 100 + (text[i + 1] - '0') * 10 + (text[i + 2] - '0');
    if (value > 255) fail(line, "\\DDD above 255");
    out += static_cast<char>(value);
    i += 2;
  }
  return out;
}

std::uint64_t parse_number(const Token& token, std::uint64_t max, std::size_t line, const char* what) {
  const std::string& text = token.text;
  if (token.quoted || text.empty() || text.size() > 10) fail(line, std::string("bad ") + what + ": " + text);
  std::uint64_t value = 0;
  for (const char c : text) {
    if (!is_digit(c)) fail(line, std::string("bad ") + what + ": " + text);
    value = value * 10 + static_cast<std::uint64_t>(c - '0');
  }
  if (value > max) fail(line, std::string(what) + " out of range: " + text);
  return value;
}

// Seconds, or a duration with units: 1w2d3h4m5s, the last number unitless if wanted.
std::uint32_t parse_duration(const Token& token, std::uint64_t max, std::size_t line, const char* what) {
  const std::string& text = token.text;
  if (token.quoted || text.empty() || text.size() > 32) fail(line, std::string("bad ") + what + ": " + text);
  std::uint64_t total = 0;
  std::uint64_t number = 0;
  bool digits = false;
  for (const char c : text) {
    if (is_digit(c)) {
      number = number * 10 + static_cast<std::uint64_t>(c - '0');
      digits = true;
      if (number > max) fail(line, std::string(what) + " out of range: " + text);
      continue;
    }
    std::uint64_t unit = 0;
    switch (std::tolower(static_cast<unsigned char>(c))) {
      case 'w':
        unit = 604800;
        break;
      case 'd':
        unit = 86400;
        break;
      case 'h':
        unit = 3600;
        break;
      case 'm':
        unit = 60;
        break;
      case 's':
        unit = 1;
        break;
      default:
        fail(line, std::string("bad ") + what + ": " + text);
    }
    if (!digits) fail(line, std::string("bad ") + what + ": " + text);
    total += number * unit;
    if (total > max) fail(line, std::string(what) + " out of range: " + text);
    number = 0;
    digits = false;
  }
  total += number;
  if (total > max) fail(line, std::string(what) + " out of range: " + text);
  return static_cast<std::uint32_t>(total);
}

bool equals_ignoring_case(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

constexpr std::array<std::pair<std::uint16_t, std::string_view>, 10> type_names{{{rr_type::a, "A"},
                                                                                 {rr_type::ns, "NS"},
                                                                                 {rr_type::cname, "CNAME"},
                                                                                 {rr_type::soa, "SOA"},
                                                                                 {rr_type::ptr, "PTR"},
                                                                                 {rr_type::mx, "MX"},
                                                                                 {rr_type::txt, "TXT"},
                                                                                 {rr_type::aaaa, "AAAA"},
                                                                                 {rr_type::srv, "SRV"},
                                                                                 {rr_type::caa, "CAA"}}};

void put16(std::vector<std::uint8_t>& out, std::uint64_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

void put32(std::vector<std::uint8_t>& out, std::uint64_t value) {
  put16(out, value >> 16);
  put16(out, value & 0xffff);
}

void put_name(std::vector<std::uint8_t>& out, const Name& name) { out.insert(out.end(), name.wire().begin(), name.wire().end()); }

int hex_value(char c) {
  if (is_digit(c)) return c - '0';
  const int lower = std::tolower(static_cast<unsigned char>(c));
  return lower >= 'a' && lower <= 'f' ? lower - 'a' + 10 : -1;
}

class Reader {
 public:
  Reader(const Name& origin, const ZoneFileLimits& limits) : _origin(origin), _limits(limits) {}

  std::vector<Record> read(std::string_view text) {
    if (text.size() > _limits.max_octets) fail(0, "longer than " + std::to_string(_limits.max_octets) + " octets");
    Lexer lexer(text);
    Line line;
    while (lexer.next(line)) {
      _line = line.number;
      const std::vector<Token>& tokens = line.tokens;
      if (!line.blank_owner && !tokens[0].quoted && tokens[0].text[0] == '$') {
        directive(tokens);
        continue;
      }
      if (_records.size() >= _limits.max_records) fail(_line, "more than " + std::to_string(_limits.max_records) + " records");
      entry(line);
    }
    return std::move(_records);
  }

 private:
  Name name(const Token& token) const {
    if (token.quoted) fail(_line, "a name cannot be quoted");
    const std::string& text = token.text;
    if (text == "@") return _origin;
    // Absolute when it ends in a dot that no backslash escapes.
    std::size_t backslashes = 0;
    for (std::size_t i = text.size() - 1; i > 0 && text[i - 1] == '\\'; --i) ++backslashes;
    const bool absolute = text.back() == '.' && backslashes % 2 == 0;
    const std::string full = absolute ? text : text + "." + (_origin.is_root() ? "" : _origin.to_text());
    const std::optional<Name> parsed = Name::from_text(full);
    if (!parsed) fail(_line, "bad name: " + text);
    return *parsed;
  }

  void directive(const std::vector<Token>& tokens) {
    const std::string& word = tokens[0].text;
    if (equals_ignoring_case(word, "$INCLUDE")) fail(_line, "$INCLUDE is not supported");
    if (tokens.size() != 2) fail(_line, word + " takes one value");
    if (equals_ignoring_case(word, "$ORIGIN")) {
      _origin = name(tokens[1]);
    } else if (equals_ignoring_case(word, "$TTL")) {
      _default_ttl = parse_duration(tokens[1], max_ttl, _line, "TTL");
    } else {
      fail(_line, "unknown directive " + word);
    }
  }

  void entry(const Line& line) {
    const std::vector<Token>& tokens = line.tokens;
    std::size_t at = 0;
    if (!line.blank_owner) {
      _owner = name(tokens[at++]);
    } else if (!_owner) {
      fail(_line, "no owner to carry over");
    }
    std::optional<std::uint32_t> ttl;
    bool klass = false;
    for (int field = 0; field < 2 && at < tokens.size(); ++field) {
      const Token& token = tokens[at];
      if (!token.quoted && is_digit(token.text[0]) && !ttl) {
        ttl = parse_duration(token, max_ttl, _line, "TTL");
      } else if (!token.quoted && !klass && (equals_ignoring_case(token.text, "IN") || equals_ignoring_case(token.text, "CLASS1"))) {
        klass = true;
      } else {
        break;
      }
      ++at;
    }
    if (at >= tokens.size()) fail(_line, "no type");
    const std::optional<std::uint16_t> type = tokens[at].quoted ? std::nullopt : type_from_text(tokens[at].text);
    if (!type) fail(_line, "unknown type or class " + tokens[at].text);
    ++at;

    if (ttl) {
      _last_ttl = ttl;
    } else if (_default_ttl) {
      ttl = _default_ttl;
    } else if (_last_ttl) {
      ttl = _last_ttl;
    } else {
      fail(_line, "no TTL, and no $TTL before it");
    }

    std::vector<std::uint8_t> rdata = read_rdata(*type, std::vector<Token>(tokens.begin() + static_cast<std::ptrdiff_t>(at), tokens.end()));
    if (rdata.size() > max_message_octets) fail(_line, "RDATA longer than 65535 octets");
    _records.push_back(Record{*_owner, *type, rr_class::in, *ttl, std::move(rdata)});
  }

  void expect_count(const std::vector<Token>& tokens, std::size_t count, std::uint16_t type) const {
    if (tokens.size() != count) fail(_line, type_to_text(type) + " takes " + std::to_string(count) + " fields");
  }

  std::vector<std::uint8_t> generic(const std::vector<Token>& tokens) const {
    if (tokens.size() < 2) fail(_line, "\\# needs a length");
    const std::uint64_t length = parse_number(tokens[1], max_message_octets, _line, "length");
    std::vector<std::uint8_t> out;
    for (std::size_t i = 2; i < tokens.size(); ++i) {
      const std::string& hex = tokens[i].text;
      if (tokens[i].quoted || hex.size() % 2 != 0) fail(_line, "bad hex: " + hex);
      for (std::size_t j = 0; j < hex.size(); j += 2) {
        const int high = hex_value(hex[j]);
        const int low = hex_value(hex[j + 1]);
        if (high < 0 || low < 0) fail(_line, "bad hex: " + hex);
        out.push_back(static_cast<std::uint8_t>(high << 4 | low));
      }
      if (out.size() > length) break;
    }
    if (out.size() != length) fail(_line, "\\# length does not match its data");
    return out;
  }

  std::vector<std::uint8_t> read_rdata(std::uint16_t type, const std::vector<Token>& tokens) const {
    if (!tokens.empty() && !tokens[0].quoted && tokens[0].text == "\\#") {
      std::vector<std::uint8_t> out = generic(tokens);
      if (!rdata_well_formed(type, out)) fail(_line, "RDATA malformed for " + type_to_text(type));
      return out;
    }
    std::vector<std::uint8_t> out;
    switch (type) {
      case rr_type::a:
      case rr_type::aaaa: {
        expect_count(tokens, 1, type);
        std::array<std::uint8_t, 16> address{};
        if (tokens[0].quoted || inet_pton(type == rr_type::a ? AF_INET : AF_INET6, tokens[0].text.c_str(), address.data()) != 1)
          fail(_line, "bad address: " + tokens[0].text);
        out.assign(address.begin(), address.begin() + (type == rr_type::a ? 4 : 16));
        break;
      }
      case rr_type::ns:
      case rr_type::cname:
      case rr_type::ptr:
        expect_count(tokens, 1, type);
        put_name(out, name(tokens[0]));
        break;
      case rr_type::mx:
        expect_count(tokens, 2, type);
        put16(out, parse_number(tokens[0], 0xffff, _line, "preference"));
        put_name(out, name(tokens[1]));
        break;
      case rr_type::srv:
        expect_count(tokens, 4, type);
        for (std::size_t i = 0; i < 3; ++i) put16(out, parse_number(tokens[i], 0xffff, _line, "SRV field"));
        put_name(out, name(tokens[3]));
        break;
      case rr_type::soa:
        expect_count(tokens, 7, type);
        put_name(out, name(tokens[0]));
        put_name(out, name(tokens[1]));
        put32(out, parse_number(tokens[2], 0xffffffff, _line, "serial"));
        for (std::size_t i = 3; i < 7; ++i) put32(out, parse_duration(tokens[i], 0xffffffff, _line, "SOA timer"));
        break;
      case rr_type::txt:
        if (tokens.empty()) fail(_line, "TXT needs a string");
        for (const Token& token : tokens) {
          const std::string text = unescape(token.text, _line);
          if (text.size() > 255) fail(_line, "a TXT string is longer than 255 octets");
          out.push_back(static_cast<std::uint8_t>(text.size()));
          out.insert(out.end(), text.begin(), text.end());
        }
        break;
      case rr_type::caa: {
        expect_count(tokens, 3, type);
        out.push_back(static_cast<std::uint8_t>(parse_number(tokens[0], 0xff, _line, "CAA flags")));
        const std::string& tag = tokens[1].text;
        if (tokens[1].quoted || tag.empty() || tag.size() > 255) fail(_line, "bad CAA tag: " + tag);
        for (const char c : tag) {
          if (!std::isalnum(static_cast<unsigned char>(c))) fail(_line, "bad CAA tag: " + tag);
        }
        out.push_back(static_cast<std::uint8_t>(tag.size()));
        out.insert(out.end(), tag.begin(), tag.end());
        const std::string value = unescape(tokens[2].text, _line);
        out.insert(out.end(), value.begin(), value.end());
        break;
      }
      default:
        fail(_line, type_to_text(type) + " is read only in the generic form \\# length hex");
    }
    return out;
  }

  Name _origin;
  ZoneFileLimits _limits;
  std::size_t _line = 0;
  std::optional<Name> _owner;
  std::optional<std::uint32_t> _default_ttl;
  std::optional<std::uint32_t> _last_ttl;
  std::vector<Record> _records;
};

// Writing.

std::string escaped_string(std::string_view octets) {
  std::string out = "\"";
  for (const char c : octets) {
    const auto octet = static_cast<std::uint8_t>(c);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (octet < 0x20 || octet >= 0x7f) {
      out += '\\';
      out += static_cast<char>('0' + octet / 100);
      out += static_cast<char>('0' + octet / 10 % 10);
      out += static_cast<char>('0' + octet % 10);
    } else {
      out += c;
    }
  }
  return out + "\"";
}

std::string generic_text(const std::vector<std::uint8_t>& rdata) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out = "\\# " + std::to_string(rdata.size());
  if (!rdata.empty()) out += ' ';
  for (const std::uint8_t octet : rdata) {
    out += digits[octet >> 4];
    out += digits[octet & 0x0f];
  }
  return out;
}

std::uint32_t get32(const std::vector<std::uint8_t>& data, std::size_t pos) {
  return static_cast<std::uint32_t>(data[pos]) << 24 | static_cast<std::uint32_t>(data[pos + 1]) << 16 | static_cast<std::uint32_t>(data[pos + 2]) << 8 |
         data[pos + 3];
}

std::uint16_t get16(const std::vector<std::uint8_t>& data, std::size_t pos) { return static_cast<std::uint16_t>(data[pos] << 8 | data[pos + 1]); }

// The presentation form of well-formed RDATA, or nothing for a type
// written only in the generic form.
std::optional<std::string> rdata_text(std::uint16_t type, const std::vector<std::uint8_t>& rdata) {
  switch (type) {
    case rr_type::a:
    case rr_type::aaaa: {
      std::array<char, INET6_ADDRSTRLEN> text{};
      inet_ntop(type == rr_type::a ? AF_INET : AF_INET6, rdata.data(), text.data(), text.size());
      return std::string(text.data());
    }
    case rr_type::ns:
    case rr_type::cname:
    case rr_type::ptr:
      return rdata_name(type, rdata)->to_text();
    case rr_type::mx:
      return std::to_string(get16(rdata, 0)) + " " + rdata_name(type, rdata)->to_text();
    case rr_type::srv:
      return std::to_string(get16(rdata, 0)) + " " + std::to_string(get16(rdata, 2)) + " " + std::to_string(get16(rdata, 4)) + " " +
             rdata_name(type, rdata)->to_text();
    case rr_type::soa: {
      const Name mname = *rdata_name(type, rdata);
      const std::size_t rname_at = mname.wire().size();
      const Name rname = *rdata_name(rr_type::ns, std::vector<std::uint8_t>(rdata.begin() + static_cast<std::ptrdiff_t>(rname_at), rdata.end() - 20));
      std::string out = mname.to_text() + " " + rname.to_text();
      for (std::size_t at = rdata.size() - 20; at < rdata.size(); at += 4) out += " " + std::to_string(get32(rdata, at));
      return out;
    }
    case rr_type::txt: {
      if (rdata.empty()) return std::nullopt;
      std::string out;
      for (std::size_t at = 0; at < rdata.size();) {
        const std::size_t length = rdata[at];
        if (at + 1 + length > rdata.size()) return std::nullopt;
        if (!out.empty()) out += ' ';
        out += escaped_string(std::string_view(reinterpret_cast<const char*>(rdata.data()) + at + 1, length));
        at += 1 + length;
      }
      return out;
    }
    case rr_type::caa: {
      if (rdata.size() < 2 || rdata[1] == 0 || 2u + rdata[1] > rdata.size()) return std::nullopt;
      const std::string_view tag(reinterpret_cast<const char*>(rdata.data()) + 2, rdata[1]);
      for (const char c : tag) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return std::nullopt;
      }
      const std::size_t value_at = 2u + rdata[1];
      return std::to_string(rdata[0]) + " " + std::string(tag) + " " +
             escaped_string(std::string_view(reinterpret_cast<const char*>(rdata.data()) + value_at, rdata.size() - value_at));
    }
    default:
      return std::nullopt;
  }
}

}  // namespace

std::optional<std::uint16_t> type_from_text(std::string_view text) {
  for (const auto& [type, name] : type_names) {
    if (equals_ignoring_case(text, name)) return type;
  }
  if (text.size() > 4 && equals_ignoring_case(text.substr(0, 4), "TYPE")) {
    const std::string_view digits = text.substr(4);
    if (digits.size() > 5) return std::nullopt;
    std::uint32_t value = 0;
    for (const char c : digits) {
      if (!is_digit(c)) return std::nullopt;
      value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (value <= 0xffff) return static_cast<std::uint16_t>(value);
  }
  return std::nullopt;
}

std::string type_to_text(std::uint16_t type) {
  for (const auto& [known, name] : type_names) {
    if (known == type) return std::string(name);
  }
  return "TYPE" + std::to_string(type);
}

std::vector<Record> parse_zone_file(std::string_view text, const Name& origin, const ZoneFileLimits& limits) { return Reader(origin, limits).read(text); }

std::string to_zone_text(const Record& record) {
  std::optional<std::string> rdata;
  if (rdata_well_formed(record.type, record.rdata)) rdata = rdata_text(record.type, record.rdata);
  return record.name.to_text() + " " + std::to_string(record.ttl) + " IN " + type_to_text(record.type) + " " + rdata.value_or(generic_text(record.rdata));
}

}  // namespace manannan::dns
