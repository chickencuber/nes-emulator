build_dir := "build"

dev:
    cmake -S . -B {{build_dir}}/dev \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_ASAN=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=1
    cmake --build {{build_dir}}/dev -j
    ln -sf build/dev/compile_commands.json compile_commands.json

# Run dev build
run-dev: dev
    {{build_dir}}/dev/nes

release:
    cmake -S . -B {{build_dir}}/release \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_ASAN=OFF
    cmake --build {{build_dir}}/release -j

run-release: release
    {{build_dir}}/release/nes

clean:
    rm -rf {{build_dir}}
