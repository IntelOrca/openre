#pragma once

#include "re2.h"

namespace openre::math
{
    Mat16& get_matrix(uint8_t type, uint8_t id);
    void rotate_matrix(Vec16p& vAngles, Mat16& m);
    void set_color_matrix(const Mat16& m);
    void set_light_matrix(const Mat16& m);
    void mul_matrix0(const Mat16& m1, const Mat16& m2, Mat16& res);
    void compare_matrix(const Mat16& m1, Mat16& m2, Mat16& res);
    void set_rot_matrix(const Mat16& m);
    void set_trans_matrix(const uint32_t* a1);

    void math_init_hooks();
}