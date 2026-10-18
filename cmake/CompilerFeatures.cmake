include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    #include <nmmintrin.h>
    int main() { unsigned long long c = 0; c = _mm_crc32_u64(c, 0x1234); return 0; }
" CIV_HAS_HW_CRC32)

if(CIV_HAS_HW_CRC32)
    target_compile_definitions(civ_core PUBLIC CIV_USE_HW_CRC32=1)
endif()