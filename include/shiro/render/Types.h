#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace shiro::render {

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

inline Vec3f operator+(const Vec3f& lhs, const Vec3f& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline Vec3f operator-(const Vec3f& lhs, const Vec3f& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Vec3f operator*(const Vec3f& lhs, float rhs) {
    return {lhs.x * rhs, lhs.y * rhs, lhs.z * rhs};
}

inline Vec3f operator*(float lhs, const Vec3f& rhs) {
    return rhs * lhs;
}

inline Vec3f operator*(const Vec3f& lhs, const Vec3f& rhs) {
    return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

inline Vec3f operator/(const Vec3f& lhs, float rhs) {
    return {lhs.x / rhs, lhs.y / rhs, lhs.z / rhs};
}

inline Vec4f operator+(const Vec4f& lhs, const Vec4f& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w};
}

inline Vec4f operator*(const Vec4f& lhs, float rhs) {
    return {lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs};
}

inline Vec4f operator*(float lhs, const Vec4f& rhs) {
    return rhs * lhs;
}

inline Vec4f operator/(const Vec4f& lhs, float rhs) {
    return {lhs.x / rhs, lhs.y / rhs, lhs.z / rhs, lhs.w / rhs};
}

inline float Dot(const Vec3f& lhs, const Vec3f& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3f Cross(const Vec3f& lhs, const Vec3f& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

inline float Length(const Vec3f& value) {
    return std::sqrt(Dot(value, value));
}

inline Vec3f Normalize(const Vec3f& value) {
    const float length = Length(value);
    return length > 0.0f ? value / length : Vec3f{};
}

inline Vec3f Clamp(const Vec3f& value, float lo, float hi) {
    return {
        std::clamp(value.x, lo, hi),
        std::clamp(value.y, lo, hi),
        std::clamp(value.z, lo, hi),
    };
}

inline float Clamp(float value, float lo, float hi) {
    return std::clamp(value, lo, hi);
}

inline Vec3f Lerp(const Vec3f& lhs, const Vec3f& rhs, float t) {
    return lhs * (1.0f - t) + rhs * t;
}

inline float MaxComponent(const Vec3f& value) {
    return std::max(value.x, std::max(value.y, value.z));
}

inline Vec3f Reflect(const Vec3f& incident, const Vec3f& normal) {
    return incident - 2.0f * Dot(incident, normal) * normal;
}

}  // namespace shiro::render
