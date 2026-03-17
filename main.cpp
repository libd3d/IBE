#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
    size_t bytes_per_line = 16;
    size_t lines = 16;
    bool show_ascii = true;
    bool show_addr = true;
    bool uppercase_hex = false;
    bool little_endian = true;
    size_t addr_width = 8; // hex digits
};

class BinaryFile {
public:
    bool open(const std::filesystem::path& p) {
        path_ = p;
        file_.close();
        file_.clear();
        file_.open(p, std::ios::binary);
        if (!file_) return false;
        file_.seekg(0, std::ios::end);
        auto end = file_.tellg();
        if (end < 0) return false;
        size_ = static_cast<uint64_t>(end);
        file_.seekg(0, std::ios::beg);
        return true;
    }

    const std::filesystem::path& path() const { return path_; }
    uint64_t size() const { return size_; }

    std::vector<uint8_t> read(uint64_t offset, size_t count) {
        std::vector<uint8_t> out;
        if (!file_) return out;
        if (offset >= size_) return out;
        uint64_t remaining = size_ - offset;
        size_t to_read = static_cast<size_t>(std::min<uint64_t>(remaining, count));
        out.resize(to_read);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(to_read));
        out.resize(static_cast<size_t>(file_.gcount()));
        return out;
    }

private:
    std::ifstream file_;
    std::filesystem::path path_;
    uint64_t size_ = 0;
};

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static bool parse_u64(std::string s, uint64_t& out) {
    s = trim(s);
    if (s.empty()) return false;
    int base = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16;
    try {
        size_t idx = 0;
        unsigned long long v = std::stoull(s, &idx, base);
        if (idx != s.size()) return false;
        out = static_cast<uint64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

static bool parse_i64(std::string s, int64_t& out) {
    s = trim(s);
    if (s.empty()) return false;
    int base = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16;
    try {
        size_t idx = 0;
        long long v = std::stoll(s, &idx, base);
        if (idx != s.size()) return false;
        out = static_cast<int64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

// Supports absolute (123/0x7B) or relative (+16, -0x10)
static bool parse_offset_expr(const std::string& expr, uint64_t current, uint64_t max_size, uint64_t& out) {
    std::string s = trim(expr);
    if (s.empty()) return false;

    if (s[0] == '+' || s[0] == '-') {
        int64_t delta = 0;
        if (!parse_i64(s, delta)) return false;
        int64_t base = current > (uint64_t)std::numeric_limits<int64_t>::max()
                           ? std::numeric_limits<int64_t>::max()
                           : (int64_t)current;
        int64_t next = base + delta;
        out = next < 0 ? 0 : (uint64_t)next;
    } else {
        uint64_t abs = 0;
        if (!parse_u64(s, abs)) return false;
        out = abs;
    }

    if (out > max_size) out = max_size;
    return true;
}

static std::vector<uint8_t> parse_hex_bytes(const std::string& text) {
    std::string s;
    s.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isxdigit(c)) s.push_back((char)c);
    }
    if (s.size() % 2 != 0) return {};

    auto hexval = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        ch = (char)std::tolower((unsigned char)ch);
        if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
        return -1;
    };

    std::vector<uint8_t> out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        int hi = hexval(s[i]);
        int lo = hexval(s[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return out;
}

static void print_hexdump(BinaryFile& bf, uint64_t offset, const Options& opt) {
    const uint64_t filesize = bf.size();
    const size_t bpl = opt.bytes_per_line;
    const size_t lines = opt.lines;

    for (size_t line = 0; line < lines; ++line) {
        uint64_t addr = offset + (uint64_t)line * (uint64_t)bpl;
        if (addr >= filesize) break;
        auto row = bf.read(addr, bpl);

        if (opt.show_addr) {
            std::ios old(nullptr);
            old.copyfmt(std::cout);
            std::cout << std::hex << std::setfill('0');
            if (opt.uppercase_hex) std::cout << std::uppercase;
            std::cout << std::setw((int)opt.addr_width) << addr << "  ";
            std::cout.copyfmt(old);
        }

        std::ios old(nullptr);
        old.copyfmt(std::cout);
        std::cout << std::hex << std::setfill('0');
        if (opt.uppercase_hex) std::cout << std::uppercase;

        for (size_t i = 0; i < bpl; ++i) {
            if (i < row.size()) std::cout << std::setw(2) << (int)row[i] << ' ';
            else std::cout << "   ";
            if (bpl == 16 && i == 7) std::cout << " ";
        }
        std::cout.copyfmt(old);

        if (opt.show_ascii) {
            std::cout << " ";
            for (size_t i = 0; i < bpl; ++i) {
                if (i < row.size()) {
                    unsigned char c = row[i];
                    std::cout << (std::isprint(c) ? (char)c : '.');
                } else {
                    std::cout << ' ';
                }
            }
        }
        std::cout << '\n';
    }
}

template <typename T>
static std::optional<T> read_integral(BinaryFile& bf, uint64_t off, bool little_endian) {
    auto bytes = bf.read(off, sizeof(T));
    if (bytes.size() != sizeof(T)) return std::nullopt;
    T v = 0;
    if (little_endian) {
        for (size_t i = 0; i < sizeof(T); ++i) v |= (T)bytes[i] << (8 * i);
    } else {
        for (size_t i = 0; i < sizeof(T); ++i) v = (T)((v << 8) | bytes[i]);
    }
    return v;
}

static void print_help() {
    std::cout <<
        "Commands (type 'help <cmd>' for details):\n"
        "  n|next                    Next page\n"
        "  p|prev                    Previous page\n"
        "  g|go <off>|+d|-d           Jump to absolute/relative offset\n"
        "  l|lines <1..256>           Rows per page\n"
        "  b|bytes <1..64>            Bytes per row\n"
        "  a|ascii on|off             Show/hide ASCII area\n"
        "  addr on|off                Show/hide address column\n"
        "  endian le|be               Set interpretation endianness\n"
        "  upper on|off               Uppercase hex output\n"
        "  i|info                     File info + view state\n"
        "  x|examine <off> <n>         Dump n bytes starting at off (one-shot)\n"
        "  r|read <off>               Interpret u8/u16/u32/u64 at offset\n"
        "  find ascii <text> [from]   Search ASCII string\n"
        "  find hex <bytes> [from]    Search hex bytes (e.g. DE AD BE EF)\n"
        "  bm add <name> [off]        Add bookmark\n"
        "  bm del <name>              Delete bookmark\n"
        "  bm list                    List bookmarks\n"
        "  bm go <name>               Jump to bookmark\n"
        "  export <off> <len> <path>  Save a slice to file\n"
        "  h|help [cmd]               Help\n"
        "  q|quit                     Quit\n";
}

static void print_help_cmd(const std::string& cmd_raw) {
    std::string cmd = to_lower(cmd_raw);
    if (cmd == "go" || cmd == "g") {
        std::cout << "Usage: go <offset>\n"
                     "  offset can be decimal (1234), hex (0x4D2), or relative (+256, -0x10).\n";
    } else if (cmd == "find") {
        std::cout << "Usage:\n"
                     "  find ascii <text> [from]\n"
                     "  find hex <bytes> [from]\n"
                     "Examples:\n"
                     "  find ascii MZ\n"
                     "  find hex DE AD BE EF 0x1000\n";
    } else if (cmd == "read" || cmd == "r") {
        std::cout << "Usage: read <offset>\n"
                     "Interprets u8/u16/u32/u64 at offset using current endianness.\n";
    } else if (cmd == "export") {
        std::cout << "Usage: export <offset> <len> <path>\n"
                     "Writes len bytes starting at offset into a new file.\n";
    } else if (cmd == "bm") {
        std::cout << "Usage:\n"
                     "  bm add <name> [offset]\n"
                     "  bm del <name>\n"
                     "  bm list\n"
                     "  bm go <name>\n";
    } else {
        print_help();
    }
}

static std::optional<uint64_t> find_bytes(BinaryFile& bf, const std::vector<uint8_t>& needle, uint64_t from) {
    if (needle.empty()) return std::nullopt;
    const uint64_t size = bf.size();
    if (from >= size) return std::nullopt;

    const size_t chunk = 1u << 20; // 1 MiB
    std::vector<uint8_t> overlap;
    overlap.reserve(needle.size() > 1 ? needle.size() - 1 : 0);

    uint64_t pos = from;
    while (pos < size) {
        size_t to_read = (size_t)std::min<uint64_t>(chunk, size - pos);
        auto buf = bf.read(pos, to_read);
        if (buf.empty()) break;

        std::vector<uint8_t> hay;
        hay.reserve(overlap.size() + buf.size());
        hay.insert(hay.end(), overlap.begin(), overlap.end());
        hay.insert(hay.end(), buf.begin(), buf.end());

        auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end());
        if (it != hay.end()) {
            uint64_t idx = (uint64_t)std::distance(hay.begin(), it);
            uint64_t base = pos - (uint64_t)overlap.size();
            return base + idx;
        }

        overlap.clear();
        if (needle.size() > 1) {
            size_t keep = std::min(needle.size() - 1, hay.size());
            overlap.insert(overlap.end(), hay.end() - keep, hay.end());
        }
        pos += (uint64_t)buf.size();
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <binary-file>\n";
        return 1;
    }

    BinaryFile bf;
    if (!bf.open(argv[1])) {
        std::cerr << "Error: Failed to open file.\n";
        return 1;
    }

    Options opt;
    uint64_t offset = 0;
    const uint64_t filesize = bf.size();
    if (filesize >= 0x100000000ULL) opt.addr_width = 16;

    std::map<std::string, uint64_t> bookmarks;

    std::cout << "Interactive Binary Explorer (Explorer Mode)\n";
    std::cout << "File: " << bf.path().string() << " (" << filesize << " bytes)\n";
    print_help();
    print_hexdump(bf, offset, opt);

    std::string cmdline;
    while (true) {
        std::cout << "\n> ";
        if (!std::getline(std::cin, cmdline)) break;
        cmdline = trim(cmdline);
        if (cmdline.empty()) continue;

        std::string cmd, subcmd;
        std::istringstream iss(cmdline);
        iss >> cmd;
        cmd = to_lower(cmd);

        if (cmd == "n" || cmd == "next") {
            uint64_t page = (uint64_t)opt.bytes_per_line * (uint64_t)opt.lines;
            if (offset + page < filesize) offset += page;
            else std::cout << "End of file reached.\n";
        } else if (cmd == "p" || cmd == "prev") {
            uint64_t page = (uint64_t)opt.bytes_per_line * (uint64_t)opt.lines;
            offset = offset >= page ? offset - page : 0;
        } else if (cmd == "g" || cmd == "go") {
            std::string expr;
            iss >> expr;
            if (expr.empty()) {
                std::cout << "Offset required.\n";
                continue;
            }
            uint64_t next = 0;
            if (!parse_offset_expr(expr, offset, filesize, next)) {
                std::cout << "Invalid offset.\n";
                continue;
            }
            offset = next;
        } else if (cmd == "l" || cmd == "lines") {
            std::string val;
            iss >> val;
            uint64_t v = 0;
            if (!parse_u64(val, v) || v == 0 || v > 256) std::cout << "Lines out of range (1-256).\n";
            else opt.lines = (size_t)v;
        } else if (cmd == "b" || cmd == "bytes") {
            std::string val;
            iss >> val;
            uint64_t v = 0;
            if (!parse_u64(val, v) || v == 0 || v > 64) std::cout << "Bytes per line out of range (1-64).\n";
            else opt.bytes_per_line = (size_t)v;
        } else if (cmd == "a" || cmd == "ascii") {
            std::string val;
            iss >> val;
            val = to_lower(val);
            if (val == "on") opt.show_ascii = true;
            else if (val == "off") opt.show_ascii = false;
            else std::cout << "Usage: ascii <on/off>\n";
        } else if (cmd == "addr") {
            std::string val;
            iss >> val;
            val = to_lower(val);
            if (val == "on") opt.show_addr = true;
            else if (val == "off") opt.show_addr = false;
            else std::cout << "Usage: addr <on/off>\n";
        } else if (cmd == "endian") {
            std::string val;
            iss >> val;
            val = to_lower(val);
            if (val == "le") opt.little_endian = true;
            else if (val == "be") opt.little_endian = false;
            else std::cout << "Usage: endian <le/be>\n";
        } else if (cmd == "upper") {
            std::string val;
            iss >> val;
            val = to_lower(val);
            if (val == "on") opt.uppercase_hex = true;
            else if (val == "off") opt.uppercase_hex = false;
            else std::cout << "Usage: upper <on/off>\n";
        } else if (cmd == "i" || cmd == "info") {
            std::cout << "File: " << bf.path().string() << "\n";
            std::cout << "Size: " << bf.size() << " bytes\n";
            std::cout << "Offset: 0x" << std::hex << offset << std::dec << " (" << offset << ")\n";
            std::cout << "View: " << opt.lines << " lines x " << opt.bytes_per_line << " bytes\n";
            std::cout << "ASCII: " << (opt.show_ascii ? "on" : "off") << ", Addr: " << (opt.show_addr ? "on" : "off") << "\n";
            std::cout << "Endian: " << (opt.little_endian ? "LE" : "BE") << ", Upper: " << (opt.uppercase_hex ? "on" : "off") << "\n";
            continue;
        } else if (cmd == "x" || cmd == "examine") {
            std::string off_s, n_s;
            iss >> off_s >> n_s;
            if (off_s.empty() || n_s.empty()) {
                std::cout << "Usage: examine <offset> <n>\n";
                continue;
            }
            uint64_t off = 0, n = 0;
            if (!parse_offset_expr(off_s, offset, filesize, off) || !parse_u64(n_s, n)) {
                std::cout << "Invalid arguments.\n";
                continue;
            }
            Options tmp = opt;
            tmp.lines = (size_t)((n + tmp.bytes_per_line - 1) / tmp.bytes_per_line);
            if (tmp.lines > 256) tmp.lines = 256;
            print_hexdump(bf, off, tmp);
            continue;
        } else if (cmd == "r" || cmd == "read") {
            std::string off_s;
            iss >> off_s;
            if (off_s.empty()) {
                std::cout << "Usage: read <offset>\n";
                continue;
            }
            uint64_t off = 0;
            if (!parse_offset_expr(off_s, offset, filesize, off)) {
                std::cout << "Invalid offset.\n";
                continue;
            }

            auto u8 = read_integral<uint8_t>(bf, off, opt.little_endian);
            auto u16 = read_integral<uint16_t>(bf, off, opt.little_endian);
            auto u32 = read_integral<uint32_t>(bf, off, opt.little_endian);
            auto u64 = read_integral<uint64_t>(bf, off, opt.little_endian);

            std::cout << "Offset 0x" << std::hex << off << std::dec << ":\n";
            if (u8) std::cout << "  u8  = " << (uint32_t)*u8 << " (0x" << std::hex << (uint32_t)*u8 << std::dec << ")\n";
            if (u16) std::cout << "  u16 = " << *u16 << " (0x" << std::hex << *u16 << std::dec << ")\n";
            if (u32) std::cout << "  u32 = " << *u32 << " (0x" << std::hex << *u32 << std::dec << ")\n";
            if (u64) std::cout << "  u64 = " << *u64 << " (0x" << std::hex << *u64 << std::dec << ")\n";
            if (!u8) std::cout << "  (not enough bytes)\n";
            continue;
        } else if (cmd == "find") {
            iss >> subcmd;
            subcmd = to_lower(subcmd);
            std::string rest;
            std::getline(iss, rest);
            rest = trim(rest);

            if (subcmd != "ascii" && subcmd != "hex") {
                std::cout << "Usage: find ascii <text> [from]\n"
                             "   or: find hex <bytes> [from]\n";
                continue;
            }

            uint64_t from = offset;
            std::string needle_text = rest;
            if (!rest.empty()) {
                std::istringstream tmp(rest);
                std::vector<std::string> tokens;
                std::string t;
                while (tmp >> t) tokens.push_back(t);
                if (tokens.size() >= 2) {
                    uint64_t abs = 0;
                    if (parse_u64(tokens.back(), abs)) {
                        from = std::min<uint64_t>(abs, filesize);
                        needle_text.clear();
                        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
                            if (i) needle_text += " ";
                            needle_text += tokens[i];
                        }
                        needle_text = trim(needle_text);
                    }
                }
            }

            std::vector<uint8_t> needle;
            if (subcmd == "ascii") needle.assign(needle_text.begin(), needle_text.end());
            else needle = parse_hex_bytes(needle_text);

            if (needle.empty()) {
                std::cout << "Empty/invalid needle.\n";
                continue;
            }

            std::cout << "Searching from 0x" << std::hex << from << std::dec << "...\n";
            auto hit = find_bytes(bf, needle, from);
            if (!hit) {
                std::cout << "Not found.\n";
            } else {
                std::cout << "Found at 0x" << std::hex << *hit << std::dec << " (" << *hit << ")\n";
                offset = *hit;
            }
        } else if (cmd == "bm") {
            iss >> subcmd;
            subcmd = to_lower(subcmd);

            if (subcmd == "add") {
                std::string name;
                iss >> name;
                std::string off_s;
                iss >> off_s;
                if (name.empty()) {
                    std::cout << "Usage: bm add <name> [offset]\n";
                    continue;
                }
                uint64_t off = offset;
                if (!off_s.empty() && !parse_offset_expr(off_s, offset, filesize, off)) {
                    std::cout << "Invalid offset.\n";
                    continue;
                }
                bookmarks[name] = off;
                std::cout << "Bookmark '" << name << "' = 0x" << std::hex << off << std::dec << "\n";
                continue;
            }

            if (subcmd == "del") {
                std::string name;
                iss >> name;
                if (name.empty()) {
                    std::cout << "Usage: bm del <name>\n";
                    continue;
                }
                if (bookmarks.erase(name)) std::cout << "Deleted bookmark '" << name << "'.\n";
                else std::cout << "No such bookmark.\n";
                continue;
            }

            if (subcmd == "list") {
                if (bookmarks.empty()) {
                    std::cout << "(no bookmarks)\n";
                } else {
                    for (const auto& [name, off] : bookmarks) {
                        std::cout << "  " << name << " = 0x" << std::hex << off << std::dec << " (" << off << ")\n";
                    }
                }
                continue;
            }

            if (subcmd == "go") {
                std::string name;
                iss >> name;
                if (name.empty()) {
                    std::cout << "Usage: bm go <name>\n";
                    continue;
                }
                auto it = bookmarks.find(name);
                if (it == bookmarks.end()) std::cout << "No such bookmark.\n";
                else offset = it->second;
            } else {
                std::cout << "Usage: bm add|del|list|go ...\n";
                continue;
            }
        } else if (cmd == "export") {
            std::string off_s, len_s;
            iss >> off_s >> len_s;
            std::string out_path;
            std::getline(iss, out_path);
            out_path = trim(out_path);
            if (off_s.empty() || len_s.empty() || out_path.empty()) {
                std::cout << "Usage: export <offset> <len> <path>\n";
                continue;
            }
            uint64_t off = 0, len = 0;
            if (!parse_offset_expr(off_s, offset, filesize, off) || !parse_u64(len_s, len)) {
                std::cout << "Invalid arguments.\n";
                continue;
            }
            if (off > filesize) off = filesize;
            uint64_t max_len = filesize - off;
            if (len > max_len) len = max_len;

            std::ofstream out(out_path, std::ios::binary);
            if (!out) {
                std::cout << "Failed to open output file.\n";
                continue;
            }

            const size_t chunk = 1u << 20;
            uint64_t written = 0;
            while (written < len) {
                size_t to_read = (size_t)std::min<uint64_t>(chunk, len - written);
                auto buf = bf.read(off + written, to_read);
                if (buf.empty()) break;
                out.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
                written += (uint64_t)buf.size();
            }
            std::cout << "Exported " << written << " bytes to " << out_path << "\n";
            continue;
        } else if (cmd == "h" || cmd == "help") {
            std::string topic;
            iss >> topic;
            if (topic.empty()) print_help();
            else print_help_cmd(topic);
            continue;
        } else if (cmd == "q" || cmd == "quit") {
            break;
        } else {
            std::cout << "Unknown command. Type 'help' for help.\n";
            continue;
        }

        print_hexdump(bf, offset, opt);
    }

    return 0;
}
