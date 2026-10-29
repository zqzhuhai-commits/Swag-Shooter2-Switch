#include "probe.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>

namespace {
constexpr uint32_t ReturnPc = 0xf0000000;
struct Region { uint32_t base; unsigned permissions; std::vector<uint8_t> bytes; };
struct Guest : Dynarmic::A32::UserCallbacks {
    std::vector<Region> regions;
    Dynarmic::A32::Jit* cpu = nullptr;
    uint64_t budget = 10000;
    bool returned = false, fault = false;
    std::string reason;
    void stop(const char* why) {
        fault = true; reason = why;
        if (cpu) cpu->HaltExecution();
    }
    Region* find(uint32_t address, size_t size, unsigned permission) {
        for (auto& r : regions)
            if (address >= r.base && size <= r.bytes.size() &&
                uint64_t(address) - r.base <= r.bytes.size() - size &&
                (r.permissions & permission) == permission) return &r;
        return nullptr;
    }
    void map(uint32_t base, size_t size, unsigned permissions) {
        if (!size || size > 128 * 1024 * 1024 || uint64_t(base) + size > (uint64_t(1) << 32))
            throw std::runtime_error("invalid guest mapping");
        for (const auto& r : regions)
            if (uint64_t(base) < uint64_t(r.base) + r.bytes.size() && uint64_t(r.base) < uint64_t(base) + size)
                throw std::runtime_error("overlapping guest mapping");
        regions.push_back({base, permissions, std::vector<uint8_t>(size)});
    }
    template<typename T> T read(uint32_t address) {
        T value{};
        auto* r = find(address, sizeof(T), PF_R);
        if (!r) { stop("unmapped_or_unreadable_guest_memory"); return value; }
        std::memcpy(&value, r->bytes.data() + address - r->base, sizeof value);
        return value;
    }
    template<typename T> void write(uint32_t address, T value) {
        auto* r = find(address, sizeof(T), PF_W);
        if (!r) { stop("unmapped_or_readonly_guest_memory"); return; }
        std::memcpy(r->bytes.data() + address - r->base, &value, sizeof value);
    }
    std::optional<uint32_t> MemoryReadCode(uint32_t a) override {
        auto* r = find(a, 4, PF_X | PF_R);
        if (!r) return std::nullopt;
        uint32_t word; std::memcpy(&word, r->bytes.data() + a - r->base, 4); return word;
    }
    uint8_t MemoryRead8(uint32_t a) override { return read<uint8_t>(a); }
    uint16_t MemoryRead16(uint32_t a) override { return read<uint16_t>(a); }
    uint32_t MemoryRead32(uint32_t a) override { return read<uint32_t>(a); }
    uint64_t MemoryRead64(uint32_t a) override { return read<uint64_t>(a); }
    void MemoryWrite8(uint32_t a, uint8_t v) override { write(a, v); }
    void MemoryWrite16(uint32_t a, uint16_t v) override { write(a, v); }
    void MemoryWrite32(uint32_t a, uint32_t v) override { write(a, v); }
    void MemoryWrite64(uint32_t a, uint64_t v) override { write(a, v); }
    void InterpreterFallback(uint32_t, size_t) override { stop("unsupported_instruction_no_interpreter"); }
    void ExceptionRaised(uint32_t, Dynarmic::A32::Exception) override { stop("guest_cpu_exception"); }
    void CallSVC(uint32_t imm) override {
        if (imm == 0 && cpu->Regs()[15] == ReturnPc + 4) {
            returned = true; cpu->HaltExecution();
        } else stop("android_syscall_or_unknown_svc_not_implemented");
    }
    void AddTicks(uint64_t ticks) override {
        budget = ticks >= budget ? 0 : budget - ticks;
        if (!budget) stop("instruction_budget_exhausted");
    }
    uint64_t GetTicksRemaining() override { return budget; }
};

bool execute(Guest& guest, uint32_t entry, uint32_t expected, FILE* log, const char* label,
             const char* expected_fault = nullptr) {
    Dynarmic::A32::UserConfig config{};
    config.callbacks = &guest;
    config.arch_version = Dynarmic::A32::ArchVersion::v7;
    config.code_cache_size = 8 * 1024 * 1024;
    // All accesses go through bounds/permissions checked callbacks. No fastmem arena.
    Dynarmic::A32::Jit jit(config);
    guest.cpu = &jit; guest.budget = 10000; guest.fault = false; guest.returned = false; guest.reason.clear();
    jit.SetCpsr(0x10 | ((entry & 1) ? 0x20 : 0));
    jit.Regs()[15] = entry & ~1u;
    jit.Regs()[14] = ReturnPc;
    jit.Run();
    const uint32_t result = jit.Regs()[0];
    bool ok = expected_fault ? guest.fault && !guest.returned && guest.reason == expected_fault
                             : guest.returned && !guest.fault && result == expected;
    std::fprintf(log, "%s result=%s r0=%u expected=%u returned=%d fault=%d reason=%s\n",
        label, ok ? "PASS" : "FAIL", result, expected, guest.returned, guest.fault, guest.reason.c_str());
    guest.cpu = nullptr;
    return ok;
}

Guest base_guest() {
    Guest guest;
    guest.map(ReturnPc, 4096, PF_R | PF_X);
    uint32_t svc = 0xef000000;
    std::memcpy(guest.regions.back().bytes.data(), &svc, 4);
    return guest;
}

std::vector<uint8_t> read_file(const char* path) {
    struct CloseFile { void operator()(FILE* f) const { std::fclose(f); } };
    std::unique_ptr<FILE, CloseFile> file(std::fopen(path, "rb"));
    if (!file) throw std::runtime_error("game library missing; run tools/prepare_test_data.py on your APK");
    if (std::fseek(file.get(), 0, SEEK_END)) throw std::runtime_error("file seek failed");
    long length = std::ftell(file.get());
    if (length < 52 || length > 128 * 1024 * 1024) throw std::runtime_error("invalid ELF file size");
    std::rewind(file.get());
    std::vector<uint8_t> data(length);
    if (std::fread(data.data(), 1, data.size(), file.get()) != data.size()) throw std::runtime_error("short file read");
    return data;
}

template<typename T> T record(const std::vector<uint8_t>& data, uint64_t offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset) throw std::runtime_error("ELF record out of bounds");
    T result; std::memcpy(&result, data.data() + offset, sizeof result); return result;
}

uint32_t load_pure_export(Guest& guest, const std::vector<uint8_t>& data, FILE* log) {
    const auto h = record<Elf32_Ehdr>(data, 0);
    if (std::memcmp(h.e_ident, ELFMAG, SELFMAG) || h.e_ident[EI_CLASS] != ELFCLASS32 ||
        h.e_ident[EI_DATA] != ELFDATA2LSB || h.e_machine != EM_ARM || h.e_type != ET_DYN ||
        h.e_ehsize != sizeof h || h.e_phentsize != sizeof(Elf32_Phdr) ||
        h.e_shentsize != sizeof(Elf32_Shdr) || !h.e_phnum || h.e_phnum > 128 || !h.e_shnum)
        throw std::runtime_error("not a supported ARM ELF32 shared library");
    // Deliberately map only PT_LOAD, at the original guest VAs. The selected export
    // must prove relocation-free below; no dynamic linking or constructors occur.
    size_t total = 0;
    for (unsigned i = 0; i < h.e_phnum; ++i) {
        auto p = record<Elf32_Phdr>(data, uint64_t(h.e_phoff) + i * sizeof(Elf32_Phdr));
        if (p.p_type != PT_LOAD || !p.p_memsz) continue;
        if (p.p_filesz > p.p_memsz || uint64_t(p.p_offset) + p.p_filesz > data.size() ||
            total + uint64_t(p.p_memsz) > 128 * 1024 * 1024) throw std::runtime_error("invalid ELF load segment");
        guest.map(p.p_vaddr, p.p_memsz, p.p_flags);
        std::memcpy(guest.regions.back().bytes.data(), data.data() + p.p_offset, p.p_filesz);
        total += p.p_memsz;
    }
    uint32_t entry = 0;
    for (unsigned i = 0; i < h.e_shnum; ++i) {
        auto s = record<Elf32_Shdr>(data, uint64_t(h.e_shoff) + i * sizeof(Elf32_Shdr));
        if (s.sh_type != SHT_DYNSYM) continue;
        if (s.sh_entsize != sizeof(Elf32_Sym) || s.sh_size % sizeof(Elf32_Sym) || s.sh_link >= h.e_shnum)
            throw std::runtime_error("invalid dynamic symbol table");
        auto names = record<Elf32_Shdr>(data, uint64_t(h.e_shoff) + s.sh_link * sizeof(Elf32_Shdr));
        if (uint64_t(names.sh_offset) + names.sh_size > data.size()) throw std::runtime_error("invalid symbol string table");
        for (size_t j = 0; j < s.sh_size / sizeof(Elf32_Sym); ++j) {
            auto symbol = record<Elf32_Sym>(data, uint64_t(s.sh_offset) + j * sizeof(Elf32_Sym));
            if (symbol.st_name >= names.sh_size) throw std::runtime_error("invalid symbol name index");
            const char* name = reinterpret_cast<const char*>(data.data() + names.sh_offset + symbol.st_name);
            if (!std::memchr(name, 0, names.sh_size - symbol.st_name)) throw std::runtime_error("unterminated symbol name");
            if (std::strcmp(name, "il2cpp_is_debugger_attached") == 0 && symbol.st_shndx != SHN_UNDEF && ELF32_ST_TYPE(symbol.st_info) == STT_FUNC)
                entry = symbol.st_value;
        }
    }
    if (!entry || (entry & 3)) throw std::runtime_error("pure ARM export unavailable");
    uint32_t pc = entry;
    bool verified = false;
    for (unsigned hop = 0; hop < 8; ++hop) {
        auto word = guest.MemoryReadCode(pc);
        if (!word) break;
        if (*word == 0xe3a00000 && guest.MemoryReadCode(pc + 4) == 0xe12fff1e) { verified = true; break; }
        if ((*word & 0xff000000) != 0xea000000) break; // unconditional ARM B only
        int32_t delta = int32_t((*word & 0x00ffffff) << 8) >> 6;
        int64_t next = int64_t(pc) + 8 + delta;
        if (next < 0 || next > std::numeric_limits<uint32_t>::max()) break;
        pc = uint32_t(next);
    }
    if (!verified) throw std::runtime_error("export is not the verified relocation-free constant-return routine; refusing execution");
    std::fprintf(log, "GAME_ELF32_MAPPED bytes=%zu export=il2cpp_is_debugger_attached entry=%08x pure_body=%08x constructors=NOT_RUN relocations=NOT_APPLIED\n", total, entry, pc);
    return entry;
}
} // namespace

int run_probe(FILE* log, const char* library_path) {
    std::fprintf(log, "SWAG2_TEST version=0.0.1 scope=ARM_THUMB_AND_PURE_GAME_EXPORT unity=NOT_STARTED\n");
    auto guest = base_guest();
    guest.map(0x02000000, 4096, PF_R | PF_X);
    const uint32_t arm[] = {0xe3a00007, 0xe2800005, 0xe12fff1e};
    std::memcpy(guest.regions.back().bytes.data(), arm, sizeof arm);
    const uint16_t thumb[] = {0x2007, 0x3005, 0x4770, 0xbf00};
    std::memcpy(guest.regions.back().bytes.data() + 32, thumb, sizeof thumb);
    const uint32_t denied_svc[] = {0xef000001, 0xe12fff1e};
    std::memcpy(guest.regions.back().bytes.data() + 64, denied_svc, sizeof denied_svc);
    const uint32_t loop = 0xeafffffe;
    std::memcpy(guest.regions.back().bytes.data() + 96, &loop, sizeof loop);
    if (!execute(guest, 0x02000000, 12, log, "ARM_ARITHMETIC")) return 1;
    if (!execute(guest, 0x02000021, 12, log, "THUMB_ARITHMETIC_AND_INTERWORK")) return 1;
    if (!execute(guest, 0x02000040, 0, log, "UNKNOWN_SVC_REJECTED",
                 "android_syscall_or_unknown_svc_not_implemented")) return 1;
    if (!execute(guest, 0x02000060, 0, log, "INFINITE_LOOP_BUDGET_STOP",
                 "instruction_budget_exhausted")) return 1;
    auto game = base_guest();
    const auto data = read_file(library_path);
    uint32_t entry = load_pure_export(game, data, log);
    if (!execute(game, entry, 0, log, "ACTUAL_GAME_ARMV7_EXPORT")) return 1;
    std::fprintf(log, "NEXT_BLOCKER full_ELF32_relocation_Android_ABI_threads_TLS_JNI_EGL_GLES_and_GC_not_implemented\n");
    return 0;
}
