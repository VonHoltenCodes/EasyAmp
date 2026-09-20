#!/bin/sh
# Fail the build if the exe imports anything Windows 98 SE does not export.
# An unresolved import does not degrade gracefully there: the loader refuses
# to start the program at all. Everything here has bitten, or nearly bitten:
#   _strtoi64/_strtoui64  pulled in by mingw's scanf family (one sscanf call)
#   _ftelli64/_fseeki64   dr_flac's stdio layer
#   the rest              NT-only kernel32 / newer msvcrt
EXE="$1"
BAD='_strtoi64|_strtoui64|_ftelli64|_fseeki64|_fstat64|_stat64|_time64|_localtime64|_mktime64|_ctime64|_vscprintf|_vscwprintf|_aligned_malloc|_aligned_free|_lock_file|_unlock_file|__acrt_iob_func|_get_output_format|_set_output_format|fopen_s|strerror_s|_snprintf_s|_vsnprintf_s|AddVectoredExceptionHandler|RemoveVectoredExceptionHandler|GetTickCount64|InitializeCriticalSectionEx|InitializeCriticalSectionAndSpinCount|GetFileSizeEx|SetFilePointerEx|IsProcessorFeaturePresent|FlsAlloc|FlsGetValue|FlsSetValue|FlsFree|GetModuleHandleExA|GetModuleHandleExW|InitOnceExecuteOnce|RtlCaptureContext|getaddrinfo|freeaddrinfo|getnameinfo|CryptAcquireContextW|GetNativeSystemInfo'
FOUND=$(i686-w64-mingw32-objdump -p "$EXE" | awk '/DLL Name:/{dll=$3; next} dll && NF>=3 && $1 ~ /^[0-9a-f]+$/ && $2 ~ /^[0-9]+$/ {print dll": "$3}' | grep -E ": ($BAD)$")
if [ -n "$FOUND" ]; then
    echo "ERROR: $EXE imports functions Windows 98 SE does not have - it would not start there:" >&2
    echo "$FOUND" | sed 's/^/    /' >&2
    exit 1
fi
echo "imports ok: nothing Windows 98 SE lacks"
