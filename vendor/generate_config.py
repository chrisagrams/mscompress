#!/usr/bin/env python3
"""
Generate config.h for base64 library for python build.
Sets all features to 0 to disable SIMD optimizations to ensure compatibility across platforms.
"""

import os


def detect_features():
    """Detect available CPU features based on platform."""
    # Disable all SIMD features to use generic codec only
    features = {
        "HAVE_AVX2": 0,
        "HAVE_NEON32": 0,
        "HAVE_NEON64": 0,
        "HAVE_SSSE3": 0,
        "HAVE_SSE41": 0,
        "HAVE_SSE42": 0,
        "HAVE_AVX": 0,
    }

    return features


def generate_config_h(output_path, features):
    """Generate config.h file with detected features."""
    config_content = ""
    for feature, value in features.items():
        config_content += f"#define {feature}   {value}\n"

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w") as f:
        f.write(config_content)

    print(f"Generated {output_path} with features:")
    for feature, value in features.items():
        if value:
            print(f"  {feature} = {value}")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    output_path = os.path.join(script_dir, "base64", "lib", "config.h")

    features = detect_features()
    generate_config_h(output_path, features)
    print(f"\nconfig.h generated successfully at: {output_path}")
