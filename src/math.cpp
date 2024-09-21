#include "math.h"
#include "interop.hpp"
#include "openre.h"
#include "re2.h"

namespace openre::math
{
    // 0x00451710
    static int rsin(int32_t angle)
    {
        const uint32_t idx = angle & 0x7FF;
        const uint32_t sign = (angle & 0x800) != 0 ? -1 : 1;

        // cosine
        if (idx >= 1024)
        {
            return sign * *((uint16_t*)0x0000527270 - idx);
        }

        return sign * static_cast<uint16_t>(gGameTable.sin_table[idx]);
    }

    // 0x00451760
    static int rcos(int32_t angle)
    {
        return rsin(angle + 1024);
    }

    // 0x00450C20
    static void mul_matrix0(Mat16& m1, Mat16& m2, Mat16& res)
    {
        interop::call<void, Mat16&, Mat16&, Mat16&>(0x00450C20, m1, m2, res);
    }

    // 0x00451120
    static void rotate_matrix_z(int32_t angle, Mat16& m)
    {
        Mat16 rot{};
        const auto c = rcos(angle);
        const auto s = rsin(angle);

        rot.m[0] = c;
        rot.m[1] = -s;
        rot.m[3] = s;
        rot.m[4] = c;
        rot.m[8] = 4096;
        mul_matrix0(rot, m, m);
    }

    // 0x004510B0
    static void rotate_matrix_y(int32_t angle, Mat16& m)
    {
        Mat16 rot{};
        const auto c = rcos(angle);
        const auto s = rsin(angle);

        rot.m[0] = c;
        rot.m[2] = s;
        rot.m[6] = -s;
        rot.m[4] = 4096;
        rot.m[8] = c;
        mul_matrix0(rot, m, m);
    }

    // 0x00451040
    static void rotate_matrix_x(int32_t angle, Mat16& m)
    {
        Mat16 rot{};
        const auto c = rcos(angle);
        const auto s = rsin(angle);

        rot.m[4] = c;
        rot.m[8] = c;
        rot.m[7] = s;
        rot.m[0] = 4096;
        rot.m[5] = -s;
        mul_matrix0(rot, m, m);
    }

    // 0x00450F60
    static void rotate_matrix(Vec16p& vAngles, Mat16& m)
    {
        memcpy(m.m, gGameTable.g_identity_mat.m, 9 * sizeof(int16_t));
        rotate_matrix_z(vAngles.z, m);
        rotate_matrix_y(vAngles.y, m);
        rotate_matrix_x(vAngles.x, m);
    }

    // 0x004512E0
    static void scale_matrix(Mat16& a1, const Vec32& a2)
    {
        // Row 1
        a1.m[0] *= a2.x / 4096;
        a1.m[1] *= a2.y / 4096;
        a1.m[2] *= a2.z / 4096;
        // Row 2
        a1.m[3] *= a2.x / 4096;
        a1.m[4] *= a2.y / 4096;
        a1.m[5] *= a2.z / 4096;
        // Row 3
        a1.m[6] *= a2.x / 4096;
        a1.m[7] *= a2.y / 4096;
        a1.m[8] *= a2.z / 4096;
    }

    void math_init_hooks()
    {
        interop::writeJmp(0x00450F60, &rotate_matrix);
        interop::writeJmp(0x00451120, &rotate_matrix_z);
        interop::writeJmp(0x004510B0, &rotate_matrix_y);
        interop::writeJmp(0x00451040, &rotate_matrix_x);
        interop::writeJmp(0x004512E0, &scale_matrix);
        interop::writeJmp(0x00451710, &rsin);
        interop::writeJmp(0x00451760, &rcos);
    }
}