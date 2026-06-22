set -e
rm -rf /workspace_build/build_*
export BUILD_TYPE="Debug"
echo "Setting up VCPKG..."
if true; then
  #
  /vcpkg/bootstrap-vcpkg.sh -disableMetrics
fi
export VCPKG_ROOT=/vcpkg
export VCPKG_INSTALLATION_ROOT=/vcpkg

echo "======================================================================"
echo "Linux GCC | Shared Lib | Unicode | Multi-thread | LTO OFF | Vcpkg"
echo "======================================================================"
export CC=gcc
export CXX=g++
cmake -S /workspace_src -B /workspace_build/build_linux_gcc_shared -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DCDD_CHARSET=UNICODE -DCDD_THREADING=ON -DCDD_DEPS=VCPKG -DC_CDD_BUILD_TESTING=ON -DC_ORM_BUILD_TESTING=ON -DC_ABSTRACT_HTTP_BUILD_TESTING=ON -DC_FS_BUILD_TESTING=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build /workspace_build/build_linux_gcc_shared --config "${BUILD_TYPE}" --parallel 4
cd /workspace_build/build_linux_gcc_shared && ctest -C "${BUILD_TYPE}" --output-on-failure
cd /workspace_build

echo "======================================================================"
echo "Linux GCC | Static Lib | ANSI | Single-thread | LTO ON | FetchContent"
echo "======================================================================"
export CC=gcc
export CXX=g++
cmake -S /workspace_src -B /workspace_build/build_linux_gcc_static -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DCDD_CHARSET=ANSI -DCDD_THREADING=OFF -DCDD_DEPS=FETCHCONTENT -DC_CDD_BUILD_TESTING=ON -DC_ORM_BUILD_TESTING=ON -DC_ABSTRACT_HTTP_BUILD_TESTING=ON -DC_FS_BUILD_TESTING=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF
cmake --build /workspace_build/build_linux_gcc_static --config "${BUILD_TYPE}" --parallel 4
cd /workspace_build/build_linux_gcc_static && ctest -C "${BUILD_TYPE}" --output-on-failure
cd /workspace_build

echo "======================================================================"
echo "Linux Clang | Static Lib | ANSI | Single-thread | LTO ON | System"
echo "======================================================================"
export CC=clang
export CXX=clang++
cmake -S /workspace_src -B /workspace_build/build_linux_clang_static -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DCDD_CHARSET=ANSI -DCDD_THREADING=OFF -DCDD_DEPS=SYSTEM -DC_CDD_BUILD_TESTING=ON -DC_ORM_BUILD_TESTING=ON -DC_ABSTRACT_HTTP_BUILD_TESTING=ON -DC_FS_BUILD_TESTING=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF
cmake --build /workspace_build/build_linux_clang_static --config "${BUILD_TYPE}" --parallel 4
cd /workspace_build/build_linux_clang_static && ctest -C "${BUILD_TYPE}" --output-on-failure
cd /workspace_build

echo "======================================================================"
echo "Linux Clang | Shared Lib | Unicode | Multi-thread | LTO OFF | Vcpkg"
echo "======================================================================"
export CC=clang
export CXX=clang++
cmake -S /workspace_src -B /workspace_build/build_linux_clang_shared -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_SHARED_LIBS=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DCDD_CHARSET=UNICODE -DCDD_THREADING=ON -DCDD_DEPS=VCPKG -DC_CDD_BUILD_TESTING=ON -DC_ORM_BUILD_TESTING=ON -DC_ABSTRACT_HTTP_BUILD_TESTING=ON -DC_FS_BUILD_TESTING=ON -DBUILD_TESTING=ON -DCDD_MSVC_RTC=OFF -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build /workspace_build/build_linux_clang_shared --config "${BUILD_TYPE}" --parallel 4
cd /workspace_build/build_linux_clang_shared && ctest -C "${BUILD_TYPE}" --output-on-failure
cd /workspace_build
