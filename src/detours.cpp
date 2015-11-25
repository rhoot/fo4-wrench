// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"
#include "detours.h"

#include "util.h"

DetourInfo::DetourInfo ()
    : buffer(nullptr) {}

DetourInfo::DetourInfo (DetourInfo&& source)
    : buffer(source.buffer)
{
    source.buffer = nullptr;
}

DetourInfo::~DetourInfo ()
{
    Reset();
}

DetourInfo& DetourInfo::operator= (DetourInfo&& source)
{
    buffer = source.buffer;
    source.buffer = nullptr;
    return *this;
}

void DetourInfo::Reset ()
{
    if (buffer) {
        VirtualFree(buffer, 0, MEM_RELEASE);
        buffer = nullptr;
    }
}

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

template <class T>
static T RelativePtr (intptr_t src, intptr_t dst, size_t extra)
{
    if (dst < src) {
        return (T)(0 - (src - dst) - extra);
    }
    return (T)(dst - (src + extra));
}

DetourInfo DetourImpl (void* src, void* dst)
{
    // Based on http://www.unknowncheats.me/forum/c-and-c/134871-64-bit-detour-function.html

    static_assert(sizeof(void*) == 8, "x64 only");
    DetourInfo info;

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
        info.buffer = (uint8_t*)VirtualAlloc(mbi.BaseAddress, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (info.buffer) {
            break;
        }
    }

    if (!info.buffer) {
        return std::move(info);
    }

    // Save the original code, and apply the detour:
    //   push   rax
    //   movabs rax, 0xcccccccccccccccc
    //   xchg   rax, [rsp]
    //   ret
    uint8_t detour[] = {0x50, 0x48, 0xb8, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x48, 0x87, 0x04, 0x24, 0xC3};
    const auto length = AsmLength(src, 16);
    if (length < 16) {
        info.Reset();
        return std::move(info);
    }

    memcpy_s(info.buffer, allocSize, src, length);
    memcpy_s(info.buffer + length, allocSize - length, detour, ArraySize(detour));
    *(uintptr_t*)(info.buffer + length + 3) = (uintptr_t)src + length;

    // Build a far jump to the destination
    uint8_t farJump[6];
    farJump[0] = 0xff;
    farJump[1] = 0x25;
    *(int32_t*)(farJump + 2) = RelativePtr<int32_t>((intptr_t)src, (intptr_t)(info.buffer + length + 16), 6);
    *(uintptr_t*)(info.buffer + length + 16) = (uintptr_t)dst;

    // Patch the source
    DWORD protection = 0;
    VirtualProtect(src, 6, PAGE_EXECUTE_READWRITE, &protection);
    memcpy(src, farJump, ArraySize(farJump));
    memset((uint8_t*)src + 6, 0x90, length - 6);
    VirtualProtect(src, 6, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), src, length);

    return std::move(info);
}
