#pragma once

#if defined(MYLOBSTER_SHARED)
    #if defined(_MSC_VER)
        #if defined(MYLOBSTER_BUILDING)
            #define MYLOBSTER_API __declspec(dllexport)
        #else
            #define MYLOBSTER_API __declspec(dllimport)
        #endif
    #elif defined(__GNUC__) || defined(__clang__)
        #if defined(MYLOBSTER_BUILDING)
            #define MYLOBSTER_API __attribute__((visibility("default")))
        #else
            #define MYLOBSTER_API
        #endif
    #else
        #define MYLOBSTER_API
    #endif
#else
    #define MYLOBSTER_API
#endif
