//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/dns/writer.h"

#include <stdexcept>

namespace manannan::dns {
namespace {

// A pointer holds a fourteen-bit offset (RFC 1035 4.1.4).
constexpr std::size_t max_pointer_offset = 0x3fff;

std::string lowered_suffix(const std::vector<std::uint8_t>& wire, std::size_t from) {
  std::string key(wire.begin() + static_cast<std::ptrdiff_t>(from), wire.end());
  for (char& c : key) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  }
  return key;
}

}  // namespace

Writer::Writer(std::size_t max_size) : _max_size(max_size), _out(header_octets, 0) {
  if (max_size < header_octets || max_size > max_message_octets) throw std::invalid_argument("DNS message size limit out of range");
}

void Writer::enter(Section section) {
  if (section < _section) throw std::logic_error("DNS message sections must be written in order");
  _section = section;
}

void Writer::put16(std::uint16_t value) {
  _out.push_back(static_cast<std::uint8_t>(value >> 8));
  _out.push_back(static_cast<std::uint8_t>(value));
}

void Writer::put32(std::uint32_t value) {
  put16(static_cast<std::uint16_t>(value >> 16));
  put16(static_cast<std::uint16_t>(value));
}

void Writer::put_name(const Name& name) {
  const std::vector<std::uint8_t>& wire = name.wire();
  for (std::size_t pos = 0; wire[pos] != 0; pos += 1 + wire[pos]) {
    std::string key = lowered_suffix(wire, pos);
    if (const auto found = _names.find(key); found != _names.end()) {
      put16(static_cast<std::uint16_t>(0xc000 | found->second));
      return;
    }
    if (_out.size() <= max_pointer_offset) {
      _names.emplace(key, static_cast<std::uint16_t>(_out.size()));
      _names_added.push_back(std::move(key));
    }
    _out.insert(_out.end(), wire.begin() + static_cast<std::ptrdiff_t>(pos), wire.begin() + static_cast<std::ptrdiff_t>(pos + 1 + wire[pos]));
  }
  _out.push_back(0);
}

bool Writer::commit_or_rollback(std::size_t mark, std::size_t names_mark, std::size_t reserved) {
  if (_out.size() + reserved <= _max_size) {
    ++_counts[static_cast<std::size_t>(_section)];
    return true;
  }
  rollback(Mark{mark, names_mark, _counts});
  return false;
}

void Writer::rollback(const Mark& mark) {
  _out.resize(mark.size);
  for (std::size_t i = mark.names; i < _names_added.size(); ++i) _names.erase(_names_added[i]);
  _names_added.resize(mark.names);
  _counts = mark.counts;
}

bool Writer::add_question(const Question& question) {
  enter(Section::question);
  const std::size_t mark = _out.size();
  const std::size_t names_mark = _names_added.size();
  put_name(question.name);
  put16(question.type);
  put16(question.klass);
  return commit_or_rollback(mark, names_mark, _reserved);
}

bool Writer::add_record(Section section, const Record& record) {
  if (section == Section::question) throw std::logic_error("a record cannot go in the question section");
  enter(section);
  if (record.rdata.size() > max_message_octets) return false;
  const std::size_t mark = _out.size();
  const std::size_t names_mark = _names_added.size();
  put_name(record.name);
  put16(record.type);
  put16(record.klass);
  put32(record.ttl);
  put16(static_cast<std::uint16_t>(record.rdata.size()));
  _out.insert(_out.end(), record.rdata.begin(), record.rdata.end());
  return commit_or_rollback(mark, names_mark, _reserved);
}

bool Writer::add_edns(const Edns& edns) {
  enter(Section::additional);
  if (_has_edns) throw std::logic_error("a DNS message carries one OPT record");
  std::size_t rdlength = 0;
  for (const EdnsOption& option : edns.options) rdlength += 4 + option.data.size();
  if (rdlength > max_message_octets) return false;
  const std::size_t mark = _out.size();
  _out.push_back(0);  // the root owns OPT
  put16(rr_type::opt);
  put16(edns.udp_payload_size);
  put32(static_cast<std::uint32_t>(edns.extended_rcode) << 24 | static_cast<std::uint32_t>(edns.version) << 16 | (edns.dnssec_ok ? 0x8000u : 0u) |
        (edns.z & 0x7fffu));
  put16(static_cast<std::uint16_t>(rdlength));
  for (const EdnsOption& option : edns.options) {
    put16(option.code);
    put16(static_cast<std::uint16_t>(option.data.size()));
    _out.insert(_out.end(), option.data.begin(), option.data.end());
  }
  _has_edns = commit_or_rollback(mark, _names_added.size(), 0);
  return _has_edns;
}

std::vector<std::uint8_t> Writer::finish() {
  _out[0] = static_cast<std::uint8_t>(_header.id >> 8);
  _out[1] = static_cast<std::uint8_t>(_header.id);
  _out[2] = static_cast<std::uint8_t>((_header.qr ? 0x80 : 0) | (_header.opcode & 0x0f) << 3 | (_header.aa ? 0x04 : 0) | (_header.tc ? 0x02 : 0) |
                                      (_header.rd ? 0x01 : 0));
  _out[3] =
      static_cast<std::uint8_t>((_header.ra ? 0x80 : 0) | (_header.z ? 0x40 : 0) | (_header.ad ? 0x20 : 0) | (_header.cd ? 0x10 : 0) | (_header.rcode & 0x0f));
  for (std::size_t i = 0; i < _counts.size(); ++i) {
    _out[4 + 2 * i] = static_cast<std::uint8_t>(_counts[i] >> 8);
    _out[5 + 2 * i] = static_cast<std::uint8_t>(_counts[i]);
  }
  return _out;
}

std::optional<std::vector<std::uint8_t>> write_message(const Message& message, std::size_t max_size) {
  Writer writer(max_size);
  writer.header() = message.header;
  for (const Question& question : message.questions) {
    if (!writer.add_question(question)) return std::nullopt;
  }
  const std::pair<Writer::Section, const std::vector<Record>*> sections[] = {
      {Writer::Section::answer, &message.answers}, {Writer::Section::authority, &message.authorities}, {Writer::Section::additional, &message.additionals}};
  for (const auto& [section, records] : sections) {
    for (const Record& record : *records) {
      if (!writer.add_record(section, record)) return std::nullopt;
    }
  }
  if (message.edns && !writer.add_edns(*message.edns)) return std::nullopt;
  return writer.finish();
}

}  // namespace manannan::dns
