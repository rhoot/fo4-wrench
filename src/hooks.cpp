// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"
#include "hooks.h"

#include "util.h"


///
// Local helpers
///

static size_t AsmLength (void* addr, size_t minSize)
{
    // TODO: I should probably set this up in a way so that it doesn't potentially dig into
    // unassigned memory. Specifically, `ud_set_input_buffer` should get a more reasonable
    // second parameter...
    //
    // Also, this function has the problem where a function can actually be too small, and
    // it'll just happily keep reading whatever's after it, unless it's an invalid opcode.

    ud_t ud;
    ud_init(&ud);
    ud_set_input_buffer(&ud, (const uint8_t*)addr, minSize + 16);
    ud_set_mode(&ud, 64);
    ud_set_syntax(&ud, nullptr);

    while (ud_insn_off(&ud) < minSize) {
        if (!ud_disassemble(&ud)) {
            break;
        }
    }

    return ud_insn_off(&ud);
}

static void* FollowJumps (void* addr)
{
    ud_t ud;
    ud_init(&ud);
    ud_set_mode(&ud, 64);
    ud_set_syntax(&ud, nullptr);

    while (true) {
        ud_set_input_buffer(&ud, (const uint8_t*)addr, 16);
        if (!ud_disassemble(&ud)) {
            ERR("Could not disassemble instruction at %p", addr);
            return addr;
        }

        const auto instruction = ud_insn_mnemonic(&ud);
        if (instruction != UD_Ijmp) {
            return addr;
        }

        const auto param = ud_insn_opr(&ud, 0);
        if (param->type != UD_OP_JIMM) {
            ERR("Invalid operand for jump: %u", param->type);
            return addr;
        }

        const auto rip = (intptr_t)addr + (intptr_t)ud_insn_len(&ud);
        addr = (void*)(rip + param->lval.sdword);
    }
}

static bool DataCompare (const uint8_t* buffer,
                         const uint8_t* data,
                         const uint8_t* sMask)
{
    for (; *sMask; ++sMask, ++buffer, ++data) {
        if (*sMask == 'x' && *buffer != *data) {
            return false;
        }
    }
    return true;
}


///
// Detour
///

namespace hooks {

    DetourBuffer::DetourBuffer (const uint8_t* data)
        : m_buffer(data) {}

    DetourBuffer::DetourBuffer (DetourBuffer&& source)
        : m_buffer(source.m_buffer)
    {
        source.m_buffer = nullptr;
    }

    DetourBuffer::~DetourBuffer ()
    {
        if (m_buffer) {
            VirtualFree((void*)m_buffer, 0, MEM_RELEASE);
        }
    }

    DetourBuffer& DetourBuffer::operator= (DetourBuffer&& source)
    {
        m_buffer = source.m_buffer;
        source.m_buffer = nullptr;
        return *this;
    }

    bool DetourBuffer::IsValid () const
    {
        return m_buffer != nullptr;
    }

} // namespace hooks

// This must be global for the forward declare in hooks::Detour<F>
hooks::DetourBuffer DetourImpl (void* src, void* dst, void** prev)
{
    // Based on http://www.unknowncheats.me/forum/c-and-c/134871-64-bit-detour-function.html

    static_assert(sizeof(void*) == 8, "x64 only");
    uint8_t* buffer = nullptr;
    *prev = nullptr;

    // MSVC really enjoys creating functions that contain nothing but a jump. That jump alone is
    // not big enough that we can detour the function. For functions where the very first
    // instruction is a jump, we therefore follow them and detour the final function.
    src = FollowJumps(src);

    // Allocate trampoline memory
    const size_t allocSize = 0x1000;

    MEMORY_BASIC_INFORMATION mbi;
    for (auto addr = (uintptr_t)src; addr > (uintptr_t)src - 0x80000000; addr = (uintptr_t)mbi.BaseAddress - 1) {
        if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
            break;
        }

        if (mbi.State != MEM_FREE) {
            continue;
        }

        // TODO: Fix bug where this will fail if the current block is too big
        buffer = (uint8_t*)VirtualAlloc(mbi.BaseAddress, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (buffer) {
            break;
        }
    }

    if (!buffer) {
        return hooks::DetourBuffer(nullptr);
    }

    // Save the original code, and apply the detour:
    //   push   rax
    //   movabs rax, 0xcccccccccccccccc
    //   xchg   rax, [rsp]
    //   ret
    uint8_t detour[] = { 0x50, 0x48, 0xb8, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x48, 0x87, 0x04, 0x24, 0xC3 };
    const auto length = AsmLength(src, 16);
    if (length < 16) {
        VirtualFree(buffer, 0, MEM_RELEASE);
        return hooks::DetourBuffer(nullptr);
    }

    memcpy_s(buffer, allocSize, src, length);
    memcpy_s(buffer + length, allocSize - length, detour, ArraySize(detour));
    *(uintptr_t*)(buffer + length + 3) = (uintptr_t)src + length;

    // Build a far jump to the destination
    uint8_t farJump[6];
    farJump[0] = 0xff;
    farJump[1] = 0x25;
    *(int32_t*)(farJump + 2) = (int32_t)((intptr_t)(buffer + length + 16) - (intptr_t)src - 6);
    *(uintptr_t*)(buffer + length + 16) = (uintptr_t)dst;

    // Patch the source
    DWORD protection = 0;
    VirtualProtect(src, 6, PAGE_EXECUTE_READWRITE, &protection);
    memcpy(src, farJump, ArraySize(farJump));
    memset((uint8_t*)src + 6, 0x90, length - 6);
    VirtualProtect(src, 6, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), src, length);

    *prev = buffer;
    return hooks::DetourBuffer(buffer);
}


///
// VfTable
///

namespace hooks {

    VfTable::VfTable ()
        : m_vftable(nullptr) { }

    VfTable::VfTable (void** vftable)
        : m_vftable(vftable) { }

    bool VfTable::IsValid () const
    {
        return m_vftable != nullptr;
    }

    VfTable& VfTable::operator= (void** vftable)
    {
        m_vftable = vftable;
        return *this;
    }

    void VfTable::Detour (size_t index, void* replacement, void** prev)
    {
        auto buffer = DetourImpl(m_vftable[index], replacement, prev);
        m_detours.emplace_back(std::move(buffer));
    }

    void VfTable::Hook (size_t index, void* replacement, void** prev)
    {
        const auto addr = m_vftable + index;
        DWORD prevProtection;

        if (!VirtualProtect(addr, sizeof(*addr), PAGE_READWRITE, &prevProtection)) {
            LOG("Failed to make %p read/writable (%lu)", addr, GetLastError());
            return;
        }

        _InterlockedExchange64((LONG64*)prev, (LONG64)*addr);
        _InterlockedExchange64((LONG64*)addr, (LONG64)replacement);

        if (!VirtualProtect(addr, sizeof(addr), prevProtection, &prevProtection)) {
            LOG("Failed to restore protection flags for %p (%lu)", addr, GetLastError());
        }
    }

} // namespace hooks


///
// Functions
///

namespace hooks {

    uintptr_t FindPattern (uintptr_t   address,
                           uintptr_t   term,
                           const char* data,
                           const char* sMask)
    {
        auto start = (const uint8_t*)address;
        auto end = (const uint8_t*)term;
        for (auto ptr = start; ptr < end; ++ptr) {
            if (DataCompare(ptr, (const uint8_t*)data, (const uint8_t*)sMask)) {
                return (uintptr_t)ptr;
            }
        }
        return 0;
    }

    struct _IMAGE_SECTION_HEADER* FindSection (const char* name,
                                               size_t      length)
    {
        auto imageBase = GetModuleHandleA(nullptr);
        auto dosHeader = (const IMAGE_DOS_HEADER*)imageBase;
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            ERR("Invalid DOS magic: %#hx", dosHeader->e_magic);
            return nullptr;
        }

        auto ntHeaders = (const IMAGE_NT_HEADERS*)((uintptr_t)imageBase + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
            ERR("Invalid PE magic: %#x", ntHeaders->Signature);
            return nullptr;
        }

        auto section = IMAGE_FIRST_SECTION(ntHeaders);

        for (unsigned i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++section, ++i) {
            if (strncmp(name, (const char*)section->Name, ArraySize(section->Name)) == 0) {
                return section;
            }
        }

        return nullptr;
    }

} // namespace hooks
