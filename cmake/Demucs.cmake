# Fetches demucs.cpp (MIT, https://github.com/sevagh/demucs.cpp) and builds its inference library
# as a static library: `stemsplitter_demucs`.
#
# We only need the library sources in src/ plus the Eigen submodule, so we don't use the upstream
# CMakeLists (which requires BLAS, libnyquist and googletest).

FetchContent_Declare(demucscpp
    GIT_REPOSITORY https://github.com/sevagh/demucs.cpp.git
    GIT_TAG        4edae497a114de1ccb17ac9b4a2b8fc0144714d7
    GIT_SUBMODULES vendor/eigen
    # Point at a folder without a CMakeLists.txt so MakeAvailable only downloads the sources.
    SOURCE_SUBDIR  _no_cmake_)
FetchContent_MakeAvailable(demucscpp)

set(DEMUCS_SRC_DIR "${demucscpp_SOURCE_DIR}/src")

add_library(stemsplitter_demucs STATIC
    ${DEMUCS_SRC_DIR}/crosstransformer.cpp
    ${DEMUCS_SRC_DIR}/dsp.cpp
    ${DEMUCS_SRC_DIR}/encdec.cpp
    ${DEMUCS_SRC_DIR}/layers.cpp
    ${DEMUCS_SRC_DIR}/lstm.cpp
    ${DEMUCS_SRC_DIR}/model_apply.cpp
    ${DEMUCS_SRC_DIR}/model_inference.cpp
    ${DEMUCS_SRC_DIR}/model_load.cpp)

target_include_directories(stemsplitter_demucs SYSTEM PUBLIC
    "${DEMUCS_SRC_DIR}"
    "${demucscpp_SOURCE_DIR}/vendor/eigen")

target_compile_features(stemsplitter_demucs PUBLIC cxx_std_17)

# Inference is unusably slow without optimisation, so always optimise this library, even in Debug.
target_compile_definitions(stemsplitter_demucs PRIVATE NDEBUG)

if(MSVC)
    target_compile_options(stemsplitter_demucs PRIVATE /O2 /fp:fast /W0)
else()
    target_compile_options(stemsplitter_demucs PRIVATE
        -O3 -w
        -fno-math-errno -freciprocal-math -fno-signed-zeros -fno-trapping-math)

    # Instruction-set flags are PUBLIC on purpose: they change Eigen's memory alignment, and every
    # translation unit that creates or frees Eigen objects must agree on it (mismatches corrupt the heap).
    if(STEMSPLITTER_NATIVE_ARCH)
        # (AVX-512 FP16 is disabled: the Eigen snapshot used by demucs.cpp doesn't build with it)
        target_compile_options(stemsplitter_demucs PUBLIC -march=native -mno-avx512fp16)
    elseif(APPLE)
        # Every Mac that runs macOS 14 Sonoma has AVX2 + FMA (Intel) or NEON (Apple silicon).
        target_compile_options(stemsplitter_demucs PUBLIC
            "SHELL:-Xarch_x86_64 -mavx2" "SHELL:-Xarch_x86_64 -mfma")
    endif()
endif()

if(APPLE AND STEMSPLITTER_USE_ACCELERATE)
    # Route Eigen's large matrix products through Apple's Accelerate framework (uses the AMX units
    # on Apple silicon). Must be defined for every translation unit that instantiates Eigen products.
    target_compile_definitions(stemsplitter_demucs PUBLIC EIGEN_USE_BLAS)
    target_link_libraries(stemsplitter_demucs PUBLIC "-framework Accelerate")
endif()

find_package(Threads REQUIRED)
target_link_libraries(stemsplitter_demucs PUBLIC Threads::Threads)
