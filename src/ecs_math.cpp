#include "ecs_math.h"

namespace openre::ecs
{
    Mat4 Mat4::identity()
    {
        Mat4 result{};
        result.m[0][0] = 1;
        result.m[1][1] = 1;
        result.m[2][2] = 1;
        result.m[3][3] = 1;
        return result;
    }

    Mat4 Mat4::translation(const Vec3& t)
    {
        Mat4 result = identity();
        result.m[3][0] = t.x;
        result.m[3][1] = t.y;
        result.m[3][2] = t.z;
        return result;
    }

    Mat4 Mat4::rotation(const Quat& q)
    {
        Mat4 result = identity();
        const float x = q.x;
        const float y = q.y;
        const float z = q.z;
        const float w = q.w;

        const float xx = x * x;
        const float yy = y * y;
        const float zz = z * z;
        const float xy = x * y;
        const float xz = x * z;
        const float yz = y * z;
        const float wx = w * x;
        const float wy = w * y;
        const float wz = w * z;

        result.m[0][0] = 1 - 2 * (yy + zz);
        result.m[0][1] = 2 * (xy - wz);
        result.m[0][2] = 2 * (xz + wy);
        result.m[1][0] = 2 * (xy + wz);
        result.m[1][1] = 1 - 2 * (xx + zz);
        result.m[1][2] = 2 * (yz - wx);
        result.m[2][0] = 2 * (xz - wy);
        result.m[2][1] = 2 * (yz + wx);
        result.m[2][2] = 1 - 2 * (xx + yy);
        return result;
    }

    Mat4 Mat4::scale(const Vec3& s)
    {
        Mat4 result = identity();
        result.m[0][0] = s.x;
        result.m[1][1] = s.y;
        result.m[2][2] = s.z;
        return result;
    }

    Mat4 Mat4::operator*(const Mat4& rhs) const
    {
        Mat4 result{};
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 4; col++)
            {
                for (int k = 0; k < 4; k++)
                    result.m[row][col] += m[row][k] * rhs.m[k][col];
            }
        }
        return result;
    }

    Vec3 Mat4::transformPoint(const Vec3& v) const
    {
        Vec3 result;
        result.x = v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + m[3][0];
        result.y = v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + m[3][1];
        result.z = v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + m[3][2];
        return result;
    }

    Mat4 trs(const Vec3& t, const Quat& q, const Vec3& s)
    {
        return Mat4::translation(t) * Mat4::rotation(q) * Mat4::scale(s);
    }
}