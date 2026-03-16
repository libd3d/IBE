#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cctype>
#include <sstream>
#include <algorithm>

void print_hexdump(const std::vector<uint8_t>& data, size_t offset, size_t bytes_per_line, size_t lines, bool show_ascii = true, bool show_addr = true) {
    size_t size = data.size();
    for (size_t line = 0; line < lines && (offset + line * bytes_per_line) < size; ++line) {
        size_t addr = offset + line * bytes_per_line;
        if (show_addr)
            std::cout << std::hex << std::setw(8) << std::setfill('0') << addr << "  ";
        for (size_t i = 0; i < bytes_per_line; ++i) {
            if ((addr + i) < size) {
                std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)data[addr + i] << ' ';
            } else {
                std::cout << "   ";
            }
        }
        if (show_ascii) {
            std::cout << " ";
            for (size_t i = 0; i < bytes_per_line; ++i) {
                if ((addr + i) < size) {
                    char c = data[addr + i];
                    std::cout << (std::isprint((unsigned char)c) ? c : '.');
                } else {
                    std::cout << ' ';
                }
            }
        }
        std::cout << '\n';
    }
}

void print_help() {
    std::cout <<
        "Commands:\n"
        "  n, next         Go to next page\n"
        "  p, prev         Go to previous page\n"
        "  g, go <offset>  Jump to offset (decimal (1234) or hex (0x4d2))\n"
        "  l, lines <num>  Set number of rows per page\n"
        "  b, bytes <num>  Set bytes shown per row\n"
        "  a, ascii <on/off>   Show/hide ASCII area\n"
        "  addr <on/off>   Show/hide address column\n"
        "  h, help         Show this help\n"
        "  q, quit         Quit the viewer\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <binary-file>\n";
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Error: Failed to open file.\n";
        return 1;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t offset = 0;
    size_t bytes_per_line = 16;
    size_t lines = 16;
    size_t filesize = buffer.size();
    bool show_ascii = true;
    bool show_addr = true;

    std::cout << "Interactive Binary Explorer (Advanced Mode)\n";
    std::cout << "File: " << argv[1] << " (" << filesize << " bytes)\n";
    print_help();

    print_hexdump(buffer, offset, bytes_per_line, lines, show_ascii, show_addr);

    std::string cmdline;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, cmdline);
        std::string cmd;
        std::istringstream iss(cmdline);
        iss >> cmd;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "n" || cmd == "next") {
            if (offset + bytes_per_line * lines < filesize) {
                offset += bytes_per_line * lines;
                if (offset >= filesize)
                    offset = filesize > bytes_per_line * lines ? filesize - bytes_per_line * lines : 0;
            } else {
                std::cout << "End of file reached.\n";
            }
        } else if (cmd == "p" || cmd == "prev") {
            if (offset >= bytes_per_line * lines) {
                offset -= bytes_per_line * lines;
            } else {
                offset = 0;
            }
        } else if (cmd == "g" || cmd == "go") {
            std::string offs;
            iss >> offs;
            if (offs.empty()) {
                std::cout << "Offset required.\n";
            } else {
                try {
                    size_t parse_off = offs.find("0x") == 0 ? std::stoul(offs, nullptr, 16) : std::stoul(offs, nullptr, 0);
                    if (parse_off > filesize) {
                        offset = filesize > bytes_per_line * lines ? filesize - bytes_per_line * lines : 0;
                    } else {
                        offset = parse_off;
                    }
                } catch (...) {
                    std::cout << "Invalid offset.\n";
                }
            }
        } else if (cmd == "l" || cmd == "lines") {
            std::string val;
            iss >> val;
            try {
                size_t lnum = std::stoul(val, nullptr, 0);
                if (lnum == 0 || lnum > 128)
                    std::cout << "Lines out of range (1-128).\n";
                else
                    lines = lnum;
            } catch (...) {
                std::cout << "Invalid number of lines.\n";
            }
        } else if (cmd == "b" || cmd == "bytes") {
            std::string val;
            iss >> val;
            try {
                size_t bnum = std::stoul(val, nullptr, 0);
                if (bnum == 0 || bnum > 64)
                    std::cout << "Bytes per line out of range (1-64).\n";
                else
                    bytes_per_line = bnum;
            } catch (...) {
                std::cout << "Invalid number of bytes.\n";
            }
        } else if (cmd == "a" || cmd == "ascii") {
            std::string val;
            iss >> val;
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            if (val == "on")
                show_ascii = true;
            else if (val == "off")
                show_ascii = false;
            else
                std::cout << "Usage: ascii <on/off>\n";
        } else if (cmd == "addr") {
            std::string val;
            iss >> val;
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            if (val == "on")
                show_addr = true;
            else if (val == "off")
                show_addr = false;
            else
                std::cout << "Usage: addr <on/off>\n";
        } else if (cmd == "q" || cmd == "quit") {
            break;
        } else if (cmd == "h" || cmd == "help") {
            print_help();
            continue;
        } else {
            std::cout << "Unknown command. Type 'h' or 'help' for help.\n";
            continue;
        }

        print_hexdump(buffer, offset, bytes_per_line, lines, show_ascii, show_addr);
    }

    return 0;
}
