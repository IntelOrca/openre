#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif // _WIN32

#include "interop.hpp"

namespace openre::interop
{
    static void* _hookTableAddress;
    static int32_t _hookTableOffset;
    constexpr int32_t kMaxHooks = 1000;
    constexpr auto kHookByteCount = 140;

    static registers _hookRegisters;
    static uintptr_t _lastHook;

    // This macro writes a little-endian 4-byte long value into *data
    // It is used to avoid type punning.
#define WRITE_ADDRESS_STRICTALIAS(data, addr)                                                                                  \
    *(data + 0) = ((addr) & 0x000000ff) >> 0;                                                                                  \
    *(data + 1) = ((addr) & 0x0000ff00) >> 8;                                                                                  \
    *(data + 2) = ((addr) & 0x00ff0000) >> 16;                                                                                 \
    *(data + 3) = ((addr) & 0xff000000) >> 24;

    static bool hookFunc(uintptr_t address, uintptr_t hookAddress, [[maybe_unused]] int32_t stacksize)
    {
        int32_t i = 0;
        uint8_t data[kHookByteCount] = { 0 };

        uintptr_t registerAddress = (uintptr_t)&_hookRegisters;

        data[i++] = 0x89; // mov [_hookRegisters], eax
        data[i++] = (0b000 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress);
        i += 4;

        data[i++] = 0x89; // mov [_hookRegisters + 4], ebx
        data[i++] = (0b011 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 4);
        i += 4;

        data[i++] = 0x89; // mov [_hookRegisters + 8], ecx
        data[i++] = (0b001 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 8);
        i += 4;

        data[i++] = 0x89; // mov [_hookRegisters + 12], edx
        data[i++] = (0b010 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 12);
        i += 4;

        data[i++] = 0x89; // mov [_hookRegisters + 16], esi
        data[i++] = (0b110 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 16);
        i += 4;

        data[i++] = 0x89; // mov [_hookRegisters + 20], edi
        data[i++] = (0b111 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 20);
        i += 4;

        data[i++] = 0x89; // mov [_hookRegisters + 24], ebp
        data[i++] = (0b101 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 24);
        i += 4;

        data[i++] = 0x8B; //  mov    eax,DWORD PTR [esp]
        data[i++] = 0x04;
        data[i++] = 0x24;
        data[i++] = 0x89; // mov [_hookRegisters], eax
        data[i++] = (0b000 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], (uintptr_t)&_lastHook);
        i += 4;

        // work out distance to nearest 0xC
        // (esp - numargs * 4) & 0xC
        // move to align - 4
        // save that amount

        // push the registers to be on the stack to access as arguments
        data[i++] = 0x68; // push _hookRegisters
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress);
        i += 4;

        data[i++] = 0xE8; // call

        WRITE_ADDRESS_STRICTALIAS(&data[i], hookAddress - address - i - 4);
        i += 4;

        data[i++] = 0x83; // add esp, 4
        data[i++] = 0xC4;
        data[i++] = 0x04;

        data[i++] = 0x25; // and eax,0xff
        data[i++] = 0xff;
        data[i++] = 0x00;
        data[i++] = 0x00;
        data[i++] = 0x00;
        data[i++] = 0xc1; // shl eax, 8
        data[i++] = 0xe0;
        data[i++] = 0x08;
        data[i++] = 0x9e; // sahf
        data[i++] = 0x9c; // pushf

        data[i++] = 0x8B; // mov eax, [_hookRegisters]
        data[i++] = (0b000 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress);
        i += 4;

        data[i++] = 0x8B; // mov ebx, [_hookRegisters + 4]
        data[i++] = (0b011 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 4);
        i += 4;

        data[i++] = 0x8B; // mov ecx, [_hookRegisters + 8]
        data[i++] = (0b001 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 8);
        i += 4;

        data[i++] = 0x8B; // mov edx, [_hookRegisters + 12]
        data[i++] = (0b010 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 12);
        i += 4;

        data[i++] = 0x8B; // mov esi, [_hookRegisters + 16]
        data[i++] = (0b110 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 16);
        i += 4;

        data[i++] = 0x8B; // mov edi, [_hookRegisters + 20]
        data[i++] = (0b111 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 20);
        i += 4;

        data[i++] = 0x8B; // mov ebp, [_hookRegisters + 24]
        data[i++] = (0b101 << 3) | 0b101;
        WRITE_ADDRESS_STRICTALIAS(&data[i], registerAddress + 24);
        i += 4;

        data[i++] = 0x9d; // popf

        data[i++] = 0xC3; // retn

        bool done;
#ifdef _WIN32
        done = WriteProcessMemory(GetCurrentProcess(), (LPVOID)address, data, i, 0);
        if (!done)
        {
            const auto errCode = static_cast<uint32_t>(GetLastError());
            fprintf(
                stderr,
                "WriteProcessMemory failed! address = 0x%08x, size = %d, GetLastError() = 0x%08x\n",
                address,
                i,
                errCode);
        }
#else
        done = true;
        // We own the pages with PROT_WRITE | PROT_EXEC, we can simply just memcpy the data
        memcpy((void*)address, data, i);
#endif // _WIN32
        return done;
    }

    void registerHook(uintptr_t address, hook_function function)
    {
        size_t size = kMaxHooks * kHookByteCount;
        if (!_hookTableAddress)
        {
#ifdef _WIN32
            _hookTableAddress = VirtualAllocEx(GetCurrentProcess(), NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (_hookTableAddress == nullptr)
            {
                const auto errCode = static_cast<uint32_t>(GetLastError());
                fprintf(stderr, "VirtualAllocEx for registerHook failed! size = %zu, GetLastError() = 0x%08x\n", size, errCode);
            }

#else
            _hookTableAddress = mmap(NULL, size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (_hookTableAddress == MAP_FAILED)
            {
                perror("mmap");
                exit(1);
            }
#endif // _WIN32
        }
        if (_hookTableOffset > kMaxHooks)
        {
            fprintf(stderr, "Failed registering hook for 0x%08x. Ran out of hook table space\n", address);
            return;
        }
        // Do a few retries here. This can fail on some versions of wine which inexplicably would fail on
        // WriteProcessMemory for specific addresses that we fully own, but skipping over failing entry would work.
        bool done = false;
        int retries = 10;
        while (!done && retries > 0 && _hookTableOffset < kMaxHooks)
        {
            uint32_t hookaddress = (uint32_t)_hookTableAddress + (_hookTableOffset * kHookByteCount);
            uint8_t data[12];
            int32_t i = 0;
            // If hook straddles page boundary, extend it with nop sled until entire hook fits in next page.
            // Hook installation takes 6 bytes, if those 6 bytes straddle page boundary, then we need at most 6 bytes of nops to
            // align
            uintptr_t page0Address = address & 0xFFFF'F000;
            uintptr_t page1Address = (address + 6) & 0xFFFF'F000;
            if (page0Address != page1Address)
            {
                uint8_t nopCount = 4096 - (address & 0xFFF);
                fprintf(
                    stdout,
                    "Address 0x%08x straddles page boundary (page0 = 0x%08x, page1 = 0x%08x), injecting %u nops\n",
                    address,
                    page0Address,
                    page1Address,
                    nopCount);
                for (; nopCount > 0; nopCount--)
                {
                    data[i++] = 0x90; // nop
                }
            }
            data[i++] = 0xE9; // jmp

            WRITE_ADDRESS_STRICTALIAS(&data[i], hookaddress - address - i - 4);
            i += 4;

            data[i++] = 0xC3; // retn

#ifdef _WIN32
            {
                DWORD oldProtect{};
                SIZE_T protectSize = i;
                BOOL protectResult = VirtualProtect(reinterpret_cast<void*>(address), protectSize, PAGE_READWRITE, &oldProtect);
                if (!protectResult)
                {
                    const auto errCode = static_cast<uint32_t>(GetLastError());
                    fprintf(
                        stderr,
                        "VirtualProtect(rw) for registerHook failed! address = 0x%08x, size = %lu, GetLastError() = 0x%08x\n",
                        address,
                        protectSize,
                        errCode);
                }
            }
#endif
            writeMemory(address, data, i);
#ifdef _WIN32
            {
                DWORD oldProtect{};
                SIZE_T protectSize = i;
                BOOL protectResult = VirtualProtect(reinterpret_cast<void*>(address), protectSize, PAGE_EXECUTE, &oldProtect);
                if (!protectResult)
                {
                    const auto errCode = static_cast<uint32_t>(GetLastError());
                    fprintf(
                        stderr,
                        "VirtualProtect(x) for registerHook failed! address = 0x%08x, size = %lu, GetLastError() = 0x%08x\n",
                        address,
                        protectSize,
                        errCode);
                }
            }
#endif

            done = hookFunc(hookaddress, (uintptr_t)function, 0);
            _hookTableOffset++;
            retries--;
            if (!done)
            {
                fprintf(stderr, "Failed registering hook for 0x%08x. Retries left: %d\n", address, retries);
            }
        }
    }

    void writeRet(uint32_t address)
    {
        uint8_t opcode = 0xC3;
        writeMemory(address, &opcode, sizeof(opcode));
    }

    void writeJmp(uint32_t address, void* fn)
    {
        uint8_t data[5] = { 0 };
        data[0] = 0xE9; // JMP

        auto addr = reinterpret_cast<uintptr_t>(fn);
        WRITE_ADDRESS_STRICTALIAS(&data[1], addr - address - 5);

        writeMemory(address, data, sizeof(data));
    }

    void writeLocoCall(uint32_t address, uint32_t fnAddress)
    {
        uint8_t data[5] = { 0 };
        data[0] = 0xE8; // CALL

        WRITE_ADDRESS_STRICTALIAS(&data[1], fnAddress - address - 5);

        writeMemory(address, data, sizeof(data));
    }

    static void* _smallHooks;
    static uint8_t* _offset;

    static void* makeJump(uint32_t address, void* fn)
    {
        size_t size = 20 * 500;

        if (!_smallHooks)
        {
#ifdef _WIN32
            _smallHooks = VirtualAllocEx(GetCurrentProcess(), NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (_smallHooks == nullptr)
            {
                const auto errCode = static_cast<uint32_t>(GetLastError());
                fprintf(stderr, "VirtualAllocEx for makeJump failed! size = %zu, GetLastError() = 0x%08x\n", size, errCode);
            }
#else
            _smallHooks = mmap(NULL, size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (_smallHooks == MAP_FAILED)
            {
                perror("mmap");
                exit(1);
            }
#endif // _WIN32
            _offset = static_cast<uint8_t*>(_smallHooks);
        }
#ifdef _WIN32
        {
            DWORD oldProtect{};
            SIZE_T protectSize = size - (static_cast<uint8_t*>(_smallHooks) - _offset);
            BOOL protectResult = VirtualProtect(reinterpret_cast<void*>(_offset), protectSize, PAGE_READWRITE, &oldProtect);
            if (!protectResult)
            {
                const auto errCode = static_cast<uint32_t>(GetLastError());
                fprintf(
                    stderr,
                    "VirtualProtect(rw) for makeJump failed! _offset = %p, size = %lu, GetLastError() = 0x%08x\n",
                    static_cast<void*>(_offset),
                    protectSize,
                    errCode);
            }
        }
#endif

        int i = 0;

        _offset[i++] = 0x58; // POP EAX

        _offset[i++] = 0x68; // PUSH
        WRITE_ADDRESS_STRICTALIAS(&_offset[i], address);
        i += 4;

        _offset[i++] = 0x50; // PUSH EAX

        uintptr_t base = reinterpret_cast<uintptr_t>(_offset);
        uintptr_t addr = reinterpret_cast<uintptr_t>(fn);

        _offset[i++] = 0xE9; // JMP
        WRITE_ADDRESS_STRICTALIAS(&_offset[i], addr - base - 12);
        i += 4;

        uint8_t* ptr = _offset;

        _offset += i;
#ifdef _WIN32
        {
            DWORD oldProtect{};
            SIZE_T protectSize = size - (static_cast<uint8_t*>(_smallHooks) - _offset);
            BOOL protectResult = VirtualProtect(reinterpret_cast<void*>(_offset), protectSize, PAGE_EXECUTE, &oldProtect);
            if (!protectResult)
            {
                const auto errCode = static_cast<uint32_t>(GetLastError());
                fprintf(
                    stderr,
                    "VirtualProtect(x) for makeJump failed! _offset = %p, size = %lu, GetLastError() = 0x%08x\n",
                    static_cast<void*>(_offset),
                    protectSize,
                    errCode);
            }
        }
#endif
        return ptr;
    }

    void hookDump(uint32_t address, void* fn)
    {
        uint8_t data[4] = { 0 };

        void* hook = makeJump(address, fn);

        uintptr_t addr = reinterpret_cast<uintptr_t>(hook);

        WRITE_ADDRESS_STRICTALIAS(&data[0], addr);

        writeMemory(address, data, sizeof(data));
    }

    void hookLib(uint32_t address, void* fn)
    {
        uint8_t data[4] = { 0 };

        uintptr_t addr = reinterpret_cast<uintptr_t>(fn);

        WRITE_ADDRESS_STRICTALIAS(&data[0], addr);

        writeMemory(address, data, sizeof(data));
    }

    void writeNop(uint32_t address, size_t count)
    {
        std::vector<uint8_t> buffer(count, 0x90);
        writeMemory(address, buffer.data(), buffer.size());
    }

    void hookThisCall(uint32_t address, void* fn)
    {
        uint8_t data[] = {
            0x87, 0x0C, 0x24,            // xchg [esp],ecx
            0x51,                        // push ecx
            0xE9, 0x00, 0x00, 0x00, 0x00 // jmp
        };

        auto addr = reinterpret_cast<uintptr_t>(fn);
        WRITE_ADDRESS_STRICTALIAS(&data[5], addr - address - 9);
        writeMemory(address, data, sizeof(data));

        // uint8_t data[] = {
        //     0x8F, 0x05, 0x00, 0x00, 0x00, 0x00, // pop [addr]
        //     0x51,                               // push ecx
        //     0xE8, 0x00, 0x00, 0x00, 0x00,       // call addr
        //     0x59,                               // pop ecx
        //     0xFF, 0x35, 0x00, 0x00, 0x00, 0x00, // push [addr]
        //     0xC3,                               // ret
        // };
        //
        // auto varAddress = reinterpret_cast<uintptr_t>(malloc(4));
        // WRITE_ADDRESS_STRICTALIAS(&data[2], varAddress);
        // WRITE_ADDRESS_STRICTALIAS(&data[15], varAddress);
        //
        // auto addr = reinterpret_cast<uintptr_t>(fn);
        // WRITE_ADDRESS_STRICTALIAS(&data[8], addr - address - 5 - 7);
        //
        // writeMemory(address, data, sizeof(data));
    }

    void* createThiscallThunk(uint32_t address, uint32_t* retStore)
    {
        auto mem = VirtualAlloc(NULL, 20, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (mem == NULL)
        {
            throw std::runtime_error("Failed to allocate memory for thiscall");
        }

        auto bytes = static_cast<uint8_t*>(mem);
        auto dst = bytes;
        *dst++ = 0x8F; // pop [addr]
        *dst++ = 0x05;
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x59; // pop ecx
        *dst++ = 0xE8; // call
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x00;
        // *dst++ = 0x51; // push ecx
        *dst++ = 0xFF; // push [addr]
        *dst++ = 0x35;
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0x00;
        *dst++ = 0xC3; // ret

        auto varAddress = reinterpret_cast<uintptr_t>(retStore);
        WRITE_ADDRESS_STRICTALIAS(&bytes[2], varAddress);
        WRITE_ADDRESS_STRICTALIAS(&bytes[14], varAddress);

        auto addr = reinterpret_cast<uintptr_t>(mem);
        WRITE_ADDRESS_STRICTALIAS(&bytes[8], address - addr - 5 - 7);

        return mem;
    }

    void deleteThiscallThunk(void* mem)
    {
        VirtualFree(mem, 0, MEM_RELEASE);
    }
}
