// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#pragma once

#include <vector>

namespace hooks {

    ///
    // Detour
    ///

    class DetourBuffer
    {
        const uint8_t* m_buffer;

        public:
            DetourBuffer (const uint8_t* data);
            DetourBuffer (const DetourBuffer&) = delete;
            DetourBuffer (DetourBuffer&& source);
            ~DetourBuffer ();

            DetourBuffer& operator= (const DetourBuffer&) = delete;
            DetourBuffer& operator= (DetourBuffer&& source);

            bool IsValid () const;
    };

    template <class F>
    DetourBuffer Detour (F* src, F* dst, F** prev)
    {
        DetourBuffer DetourImpl(void*, void*, void**);
        return DetourImpl((void*)src, (void*)dst, (void**)prev);
    }


    ///
    // Function
    ///

    template <size_t I, class F>
    struct Function;

    template <size_t I, class R, class... A>
    struct Function<I, R(A...)> {
        static const size_t INDEX = I;
        using Ret = R;
        using Fn = R(A...);
    };


    ///
    // VfTable
    ///

    class VfTable
    {
        void** m_vftable;
        std::vector<DetourBuffer> m_detours;

        void Detour (size_t index, void* replacement, void** prev);
        void Hook (size_t index, void* replacement, void** prev);

        public:
            VfTable ();
            VfTable (void** vftable);
            bool IsValid () const;
            VfTable& operator= (void** vftable);

            template <class F>
            void Detour (typename F::Fn* replacement, typename F::Fn** prev)
            {
                Detour(F::INDEX, (void*)replacement, (void**)prev);
            }

            template <class F>
            void Hook (typename F::Fn* replacement, typename F::Fn** prev)
            {
                Hook(F::INDEX, (void*)replacement, (void**)prev);
            }

            template <class F, class... Args>
            typename F::Ret Invoke (Args&& ... args)
            {
                auto fn = (typename F::Fn*)m_vftable[F::INDEX];
                return fn(std::forward<Args>(args) ...);
            }
    };


    ///
    // Functions
    ///

    uintptr_t FindPattern (uintptr_t address, uintptr_t term, const char* data, const char* sMask);
    struct _IMAGE_SECTION_HEADER* FindSection (const char* name, size_t length);

} // namespace hooks
