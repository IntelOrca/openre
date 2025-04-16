#pragma once

#include "re2.h"

namespace openre::math
{
    Mat16& get_matrix(uint8_t type, uint8_t id);
    void rotate_matrix(Vec16p& vAngles, Mat16& m);
    void set_color_matrix(const Mat16& m);
    void set_light_matrix(const Mat16& m);
    void mul_matrix0(Mat16& left, Mat16& right, Mat16& res);
    void compare_matrix(Mat16& left, Mat16& right, Mat16& res);
    void set_rot_matrix(const Mat16& m);
    void set_trans_matrix(const uint32_t* a1);
    void apply_matrixsv(const Mat16& m, const Vec16& v1, Vec16& res);

    void math_init_hooks();
}