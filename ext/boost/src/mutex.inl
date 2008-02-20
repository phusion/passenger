// Copyright (C) 2001-2003
// William E. Kempf
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
// boostinspect:nounnamed

namespace {

#if defined(BOOST_HAS_WINTHREADS)
//:PREVENT THIS FROM BEING DUPLICATED
typedef BOOL (WINAPI* TryEnterCriticalSection_type)(LPCRITICAL_SECTION lpCriticalSection);
TryEnterCriticalSection_type g_TryEnterCriticalSection = 0;
boost::once_flag once_init_TryEnterCriticalSection = BOOST_ONCE_INIT;

void init_TryEnterCriticalSection()
{
    //TryEnterCriticalSection is only available on WinNT 4.0 or later; 
    //it is not available on Win9x.

    OSVERSIONINFO version_info = {sizeof(OSVERSIONINFO)};
    ::GetVersionEx(&version_info);
    if (version_info.dwPlatformId == VER_PLATFORM_WIN32_NT && 
        version_info.dwMajorVersion >= 4)
    {
        if (HMODULE kernel_module = GetModuleHandle(TEXT("KERNEL32.DLL")))
        {
            g_TryEnterCriticalSection = reinterpret_cast<TryEnterCriticalSection_type>(
#if defined(BOOST_NO_ANSI_APIS)
                GetProcAddressW(kernel_module, L"TryEnterCriticalSection")
#else
                GetProcAddress(kernel_module, "TryEnterCriticalSection")
#endif        
                );
        }
    }
}

inline bool has_TryEnterCriticalSection()
{
    boost::call_once(init_TryEnterCriticalSection, once_init_TryEnterCriticalSection);
    return g_TryEnterCriticalSection != 0;
}

inline HANDLE mutex_cast(void* p)
{
    return reinterpret_cast<HANDLE>(p);
}

inline LPCRITICAL_SECTION critical_section_cast(void* p)
{
    return reinterpret_cast<LPCRITICAL_SECTION>(p);
}

inline void* new_critical_section()
{
    try
    {
        LPCRITICAL_SECTION critical_section = new CRITICAL_SECTION;
        if (critical_section == 0) throw boost::thread_resource_error();
        InitializeCriticalSection(critical_section);
        return critical_section;
    }
    catch(...)
    {
        throw boost::thread_resource_error();
    }
}

inline void* new_mutex(const char* name)
{
#if defined(BOOST_NO_ANSI_APIS)
    USES_CONVERSION;
    HANDLE mutex = CreateMutexW(0, 0, A2CW(name));
#else
    HANDLE mutex = CreateMutexA(0, 0, name);
#endif
    if (mutex == 0 || mutex == INVALID_HANDLE_VALUE) //:xxx (check for both values?)
        throw boost::thread_resource_error();
    return reinterpret_cast<void*>(mutex);
}

inline void delete_critical_section(void* mutex)
{
    DeleteCriticalSection(critical_section_cast(mutex));
    delete critical_section_cast(mutex);
}

inline void delete_mutex(void* mutex)
{
    int res = 0;
    res = CloseHandle(mutex_cast(mutex));
    assert(res);
}

inline void wait_critical_section_infinite(void* mutex)
{
    EnterCriticalSection(critical_section_cast(mutex)); //:xxx Can throw an exception under low memory conditions
}

inline bool wait_critical_section_try(void* mutex)
{
    BOOL res = g_TryEnterCriticalSection(critical_section_cast(mutex));
    return res != 0;
}

inline int wait_mutex(void* mutex, int time)
{
    unsigned int res = 0;
    res = WaitForSingleObject(mutex_cast(mutex), time);
//:xxx    assert(res != WAIT_FAILED && res != WAIT_ABANDONED);
    return res;
}

inline void release_critical_section(void* mutex)
{
    LeaveCriticalSection(critical_section_cast(mutex));
}

inline void release_mutex(void* mutex)
{
    BOOL res = FALSE;
    res = ReleaseMutex(mutex_cast(mutex));
    assert(res);
}
#endif

}
