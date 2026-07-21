#include "vm.h"

#include <unicorn/unicorn.h>

#include <elfio/elfio.hpp>
#include <format>
#include <fstream>

#include "mm.h"

namespace vlinux {
VM::VM() {
    uc_err result;
    result = uc_open(UC_ARCH_X86, UC_MODE_64, &this->engine_);
    if (result != UC_ERR_OK) {
        throw std::runtime_error(
            std::format("uc_open failed: {}", uc_strerror(result)));
    }
}

VM::~VM() { uc_close(this->engine_); }

void VM::reset() {}

void VM::load(const std::filesystem::path& path) {
    ELFIO::elfio reader;
    if (!reader.load(path)) {
        throw std::runtime_error(
            std::format("failed to load elf file {}", path.string()));
    }

    for (const auto& segment : reader.segments) {
        if (segment->get_type() != ELFIO::PT_LOAD) {
            // We only care about LOAD segments.
            continue;
        }

        auto file_offset = segment->get_offset();
        auto file_size = segment->get_file_size();
        auto virtual_address = segment->get_virtual_address();
        auto virtual_size = segment->get_memory_size();

        // Read the segment data from the file.
        std::vector<char> data;
        std::ifstream file(path.string(), std::ios::binary);
        file.seekg(file_offset);
        data.resize(file_size);
        file.read(data.data(), file_size);
        file.close();

        // Create memory mapping.
        std::uint32_t perms = 0;
        auto segment_flags = segment->get_flags();
        if (segment_flags & ELFIO::PF_R) {
            perms |= UC_PROT_READ;
        }
        if (segment_flags & ELFIO::PF_W) {
            perms |= UC_PROT_WRITE;
        }
        if (segment_flags & ELFIO::PF_X) {
            perms |= UC_PROT_EXEC;
        }
        uc_mem_map(this->engine_, virtual_address,
                   (virtual_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1), perms);

        // Write the segment data to the memory.
        uc_mem_write(this->engine_, virtual_address, data.data(), file_size);
        std::vector<char> zeros(virtual_size - file_size);
        uc_mem_write(this->engine_, virtual_address + file_size, zeros.data(),
                     virtual_size - file_size);
    }

    // Set the entrypoint.
    this->entrypoint_ = reader.get_entry();
}

void VM::run() {}
}  // namespace vlinux
