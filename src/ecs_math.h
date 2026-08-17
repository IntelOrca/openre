#pragma once

namespace openre::ecs
{
    struct Vec3
    {
        float x = 0;
        float y = 0;
        float z = 0;
    };

    struct Quat
    {
        float x = 0;
        float y = 0;
        float z = 0;
        float w = 1;
    };

    /// 4x4 matrix, stored row-major (m[row][col]).
    ///
    /// Vectors are treated as rows, so a point is transformed with v' = v * M
    /// and the translation lives in the bottom row (m[3][0..2]).
    struct Mat4
    {
        float m[4][4] = {};

        static Mat4 identity();
        static Mat4 translation(const Vec3& t);
        static Mat4 rotation(const Quat& q);
        static Mat4 scale(const Vec3& s);

        Mat4 operator*(const Mat4& rhs) const;
        Vec3 transformPoint(const Vec3& v) const;
    };

    /// Composes translation * rotation * scale (scale applied first).
    Mat4 trs(const Vec3& t, const Quat& q, const Vec3& s);
}