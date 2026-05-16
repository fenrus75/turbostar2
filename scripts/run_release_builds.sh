#!/bin/bash
set -e

# Define build types
builds=("build" "build_release" "build_debug" "build_acov" "build_cov" "build_ubsan" "build_msan" "build-tsan")

echo "Starting release build verification..."

for b in "${builds[@]}"; do
    echo "Processing $b..."
    
    # Configure based on build type
    case $b in
        "build")
            meson setup --wipe $b
            ;;
        "build_release")
            meson setup --wipe --buildtype=release $b
            ;;
        "build_debug")
            meson setup --wipe --buildtype=debug $b
            ;;
        "build_acov")
            meson setup --wipe -Denable-tests=true -Db_sanitize=address $b
            ;;
        "build_cov")
            meson setup --wipe -Denable-tests=true $b
            ;;
        "build_ubsan")
            meson setup --wipe -Denable-tests=true -Db_sanitize=undefined $b
            ;;
        "build_msan")
            meson setup --wipe -Denable-tests=true -Db_sanitize=memory $b
            ;;
        "build-tsan")
            meson setup --wipe -Denable-tests=true -Db_sanitize=thread $b
            ;;
    esac

    # Build
    ninja -C $b

    # Test
    meson test -C $b
    
    echo "$b verification passed."
done

echo "All release build checks passed!"
