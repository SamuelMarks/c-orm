set -e
rm -rf build_*
export BUILD_TYPE="Debug"
export VCPKG_ROOT=~/repos/vcpkg
export VCPKG_INSTALLATION_ROOT=~/repos/vcpkg

echo "======================================================================"
echo "Linux GCC | Shared Lib | Unicode | Multi-thread | LTO OFF | Vcpkg"
echo "======================================================================"
export CC=gcc
export CXX=g++
cmake -S . -B build_linux_gcc_shared -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DCDD_CHARSET=UNICODE -DCDD_THREADING=ON -DCDD_DEPS=VCPKG -DC_CDD_BUILD_TESTS=ON -DC_ORM_BUILD_TESTS=ON -DC_ABSTRACT_HTTP_BUILD_TESTS=ON -DC_FS_BUILD_TESTS=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build_linux_gcc_shared --config "${BUILD_TYPE}" --parallel 4
cd build_linux_gcc_shared && ctest -C "${BUILD_TYPE}" --output-on-failure
cd ..

echo "======================================================================"
echo "Linux GCC | Static Lib | ANSI | Single-thread | LTO ON | FetchContent"
echo "======================================================================"
export CC=gcc
export CXX=g++
cmake -S . -B build_linux_gcc_static -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DCDD_CHARSET=ANSI -DCDD_THREADING=OFF -DCDD_DEPS=FETCHCONTENT -DC_CDD_BUILD_TESTS=ON -DC_ORM_BUILD_TESTS=ON -DC_ABSTRACT_HTTP_BUILD_TESTS=ON -DC_FS_BUILD_TESTS=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF
cmake --build build_linux_gcc_static --config "${BUILD_TYPE}" --parallel 4
cd build_linux_gcc_static && ctest -C "${BUILD_TYPE}" --output-on-failure
cd ..

echo "======================================================================"
echo "Linux Clang | Static Lib | ANSI | Single-thread | LTO ON | System"
echo "======================================================================"
export CC=clang
export CXX=clang++
cmake -S . -B build_linux_clang_static -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DCDD_CHARSET=ANSI -DCDD_THREADING=OFF -DCDD_DEPS=SYSTEM -DC_CDD_BUILD_TESTS=ON -DC_ORM_BUILD_TESTS=ON -DC_ABSTRACT_HTTP_BUILD_TESTS=ON -DC_FS_BUILD_TESTS=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF -DCMAKE_C_FLAGS="-resource-dir /tmp/clang19" -DCMAKE_EXE_LINKER_FLAGS="-resource-dir /tmp/clang19"
cmake --build build_linux_clang_static --config "${BUILD_TYPE}" --parallel 4
cd build_linux_clang_static && ctest -C "${BUILD_TYPE}" --output-on-failure
cd ..

echo "======================================================================"
echo "Linux Clang | Shared Lib | Unicode | Multi-thread | LTO OFF | Vcpkg"
echo "======================================================================"
export CC=clang
export CXX=clang++
cmake -S . -B build_linux_clang_shared -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DCDD_CHARSET=UNICODE -DCDD_THREADING=ON -DCDD_DEPS=VCPKG -DC_CDD_BUILD_TESTS=ON -DC_ORM_BUILD_TESTS=ON -DC_ABSTRACT_HTTP_BUILD_TESTS=ON -DC_FS_BUILD_TESTS=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF -DCMAKE_C_FLAGS="-resource-dir /tmp/clang19" -DCMAKE_EXE_LINKER_FLAGS="-resource-dir /tmp/clang19" -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake -DCMAKE_C_FLAGS="-resource-dir /tmp/clang19" -DCMAKE_EXE_LINKER_FLAGS="-resource-dir /tmp/clang19"
cmake --build build_linux_clang_shared --config "${BUILD_TYPE}" --parallel 4
cd build_linux_clang_shared && ctest -C "${BUILD_TYPE}" --output-on-failure
cd ..
