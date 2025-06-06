#include "math.h"
#include "interop.hpp"
#include "openre.h"

namespace openre::math
{
    // Precomputed constant values for sine[0-1023] and cosine[1024-2037]
    static const uint16_t gCosSinTable[2048] = {
        0x0000, 0x0006, 0x000d, 0x0013, 0x0019, 0x001f, 0x0026, 0x002c, 0x0032, 0x0039, 0x003f, 0x0045, 0x004b, 0x0052, 0x0058,
        0x005e, 0x0065, 0x006b, 0x0071, 0x0077, 0x007e, 0x0084, 0x008a, 0x0090, 0x0097, 0x009d, 0x00a3, 0x00aa, 0x00b0, 0x00b6,
        0x00bc, 0x00c3, 0x00c9, 0x00cf, 0x00d6, 0x00dc, 0x00e2, 0x00e8, 0x00ef, 0x00f5, 0x00fb, 0x0101, 0x0108, 0x010e, 0x0114,
        0x011b, 0x0121, 0x0127, 0x012d, 0x0134, 0x013a, 0x0140, 0x0146, 0x014d, 0x0153, 0x0159, 0x015f, 0x0166, 0x016c, 0x0172,
        0x0178, 0x017f, 0x0185, 0x018b, 0x0191, 0x0198, 0x019e, 0x01a4, 0x01aa, 0x01b1, 0x01b7, 0x01bd, 0x01c3, 0x01ca, 0x01d0,
        0x01d6, 0x01dc, 0x01e3, 0x01e9, 0x01ef, 0x01f5, 0x01fc, 0x0202, 0x0208, 0x020e, 0x0215, 0x021b, 0x0221, 0x0227, 0x022d,
        0x0234, 0x023a, 0x0240, 0x0246, 0x024d, 0x0253, 0x0259, 0x025f, 0x0265, 0x026c, 0x0272, 0x0278, 0x027e, 0x0284, 0x028b,
        0x0291, 0x0297, 0x029d, 0x02a3, 0x02aa, 0x02b0, 0x02b6, 0x02bc, 0x02c2, 0x02c9, 0x02cf, 0x02d5, 0x02db, 0x02e1, 0x02e8,
        0x02ee, 0x02f4, 0x02fa, 0x0300, 0x0306, 0x030d, 0x0313, 0x0319, 0x031f, 0x0325, 0x032b, 0x0332, 0x0338, 0x033e, 0x0344,
        0x034a, 0x0350, 0x0356, 0x035d, 0x0363, 0x0369, 0x036f, 0x0375, 0x037b, 0x0381, 0x0388, 0x038e, 0x0394, 0x039a, 0x03a0,
        0x03a6, 0x03ac, 0x03b2, 0x03b9, 0x03bf, 0x03c5, 0x03cb, 0x03d1, 0x03d7, 0x03dd, 0x03e3, 0x03e9, 0x03ef, 0x03f6, 0x03fc,
        0x0402, 0x0408, 0x040e, 0x0414, 0x041a, 0x0420, 0x0426, 0x042c, 0x0432, 0x0438, 0x043e, 0x0444, 0x044b, 0x0451, 0x0457,
        0x045d, 0x0463, 0x0469, 0x046f, 0x0475, 0x047b, 0x0481, 0x0487, 0x048d, 0x0493, 0x0499, 0x049f, 0x04a5, 0x04ab, 0x04b1,
        0x04b7, 0x04bd, 0x04c3, 0x04c9, 0x04cf, 0x04d5, 0x04db, 0x04e1, 0x04e7, 0x04ed, 0x04f3, 0x04f9, 0x04ff, 0x0505, 0x050b,
        0x0511, 0x0517, 0x051d, 0x0523, 0x0529, 0x052f, 0x0534, 0x053a, 0x0540, 0x0546, 0x054c, 0x0552, 0x0558, 0x055e, 0x0564,
        0x056a, 0x0570, 0x0576, 0x057c, 0x0581, 0x0587, 0x058d, 0x0593, 0x0599, 0x059f, 0x05a5, 0x05ab, 0x05b1, 0x05b6, 0x05bc,
        0x05c2, 0x05c8, 0x05ce, 0x05d4, 0x05da, 0x05df, 0x05e5, 0x05eb, 0x05f1, 0x05f7, 0x05fd, 0x0602, 0x0608, 0x060e, 0x0614,
        0x061a, 0x061f, 0x0625, 0x062b, 0x0631, 0x0637, 0x063c, 0x0642, 0x0648, 0x064e, 0x0654, 0x0659, 0x065f, 0x0665, 0x066b,
        0x0670, 0x0676, 0x067c, 0x0682, 0x0687, 0x068d, 0x0693, 0x0699, 0x069e, 0x06a4, 0x06aa, 0x06af, 0x06b5, 0x06bb, 0x06c1,
        0x06c6, 0x06cc, 0x06d2, 0x06d7, 0x06dd, 0x06e3, 0x06e8, 0x06ee, 0x06f4, 0x06f9, 0x06ff, 0x0705, 0x070a, 0x0710, 0x0715,
        0x071b, 0x0721, 0x0726, 0x072c, 0x0732, 0x0737, 0x073d, 0x0742, 0x0748, 0x074e, 0x0753, 0x0759, 0x075e, 0x0764, 0x076a,
        0x076f, 0x0775, 0x077a, 0x0780, 0x0785, 0x078b, 0x0790, 0x0796, 0x079b, 0x07a1, 0x07a6, 0x07ac, 0x07b2, 0x07b7, 0x07bd,
        0x07c2, 0x07c8, 0x07cd, 0x07d2, 0x07d8, 0x07dd, 0x07e3, 0x07e8, 0x07ee, 0x07f3, 0x07f9, 0x07fe, 0x0804, 0x0809, 0x080e,
        0x0814, 0x0819, 0x081f, 0x0824, 0x082a, 0x082f, 0x0834, 0x083a, 0x083f, 0x0845, 0x084a, 0x084f, 0x0855, 0x085a, 0x085f,
        0x0865, 0x086a, 0x086f, 0x0875, 0x087a, 0x087f, 0x0885, 0x088a, 0x088f, 0x0895, 0x089a, 0x089f, 0x08a5, 0x08aa, 0x08af,
        0x08b4, 0x08ba, 0x08bf, 0x08c4, 0x08c9, 0x08cf, 0x08d4, 0x08d9, 0x08de, 0x08e4, 0x08e9, 0x08ee, 0x08f3, 0x08f8, 0x08fe,
        0x0903, 0x0908, 0x090d, 0x0912, 0x0918, 0x091d, 0x0922, 0x0927, 0x092c, 0x0931, 0x0937, 0x093c, 0x0941, 0x0946, 0x094b,
        0x0950, 0x0955, 0x095a, 0x095f, 0x0965, 0x096a, 0x096f, 0x0974, 0x0979, 0x097e, 0x0983, 0x0988, 0x098d, 0x0992, 0x0997,
        0x099c, 0x09a1, 0x09a6, 0x09ab, 0x09b0, 0x09b5, 0x09ba, 0x09bf, 0x09c4, 0x09c9, 0x09ce, 0x09d3, 0x09d8, 0x09dd, 0x09e2,
        0x09e7, 0x09ec, 0x09f1, 0x09f6, 0x09fb, 0x09ff, 0x0a04, 0x0a09, 0x0a0e, 0x0a13, 0x0a18, 0x0a1d, 0x0a22, 0x0a26, 0x0a2b,
        0x0a30, 0x0a35, 0x0a3a, 0x0a3f, 0x0a44, 0x0a48, 0x0a4d, 0x0a52, 0x0a57, 0x0a5c, 0x0a60, 0x0a65, 0x0a6a, 0x0a6f, 0x0a73,
        0x0a78, 0x0a7d, 0x0a82, 0x0a86, 0x0a8b, 0x0a90, 0x0a95, 0x0a99, 0x0a9e, 0x0aa3, 0x0aa7, 0x0aac, 0x0ab1, 0x0ab5, 0x0aba,
        0x0abf, 0x0ac3, 0x0ac8, 0x0acd, 0x0ad1, 0x0ad6, 0x0adb, 0x0adf, 0x0ae4, 0x0ae8, 0x0aed, 0x0af2, 0x0af6, 0x0afb, 0x0aff,
        0x0b04, 0x0b08, 0x0b0d, 0x0b11, 0x0b16, 0x0b1b, 0x0b1f, 0x0b24, 0x0b28, 0x0b2d, 0x0b31, 0x0b36, 0x0b3a, 0x0b3e, 0x0b43,
        0x0b47, 0x0b4c, 0x0b50, 0x0b55, 0x0b59, 0x0b5e, 0x0b62, 0x0b66, 0x0b6b, 0x0b6f, 0x0b74, 0x0b78, 0x0b7c, 0x0b81, 0x0b85,
        0x0b89, 0x0b8e, 0x0b92, 0x0b97, 0x0b9b, 0x0b9f, 0x0ba3, 0x0ba8, 0x0bac, 0x0bb0, 0x0bb5, 0x0bb9, 0x0bbd, 0x0bc1, 0x0bc6,
        0x0bca, 0x0bce, 0x0bd2, 0x0bd7, 0x0bdb, 0x0bdf, 0x0be3, 0x0be8, 0x0bec, 0x0bf0, 0x0bf4, 0x0bf8, 0x0bfc, 0x0c01, 0x0c05,
        0x0c09, 0x0c0d, 0x0c11, 0x0c15, 0x0c19, 0x0c1e, 0x0c22, 0x0c26, 0x0c2a, 0x0c2e, 0x0c32, 0x0c36, 0x0c3a, 0x0c3e, 0x0c42,
        0x0c46, 0x0c4a, 0x0c4e, 0x0c52, 0x0c56, 0x0c5a, 0x0c5e, 0x0c62, 0x0c66, 0x0c6a, 0x0c6e, 0x0c72, 0x0c76, 0x0c7a, 0x0c7e,
        0x0c82, 0x0c86, 0x0c8a, 0x0c8e, 0x0c91, 0x0c95, 0x0c99, 0x0c9d, 0x0ca1, 0x0ca5, 0x0ca9, 0x0cac, 0x0cb0, 0x0cb4, 0x0cb8,
        0x0cbc, 0x0cc0, 0x0cc3, 0x0cc7, 0x0ccb, 0x0ccf, 0x0cd2, 0x0cd6, 0x0cda, 0x0cde, 0x0ce1, 0x0ce5, 0x0ce9, 0x0ced, 0x0cf0,
        0x0cf4, 0x0cf8, 0x0cfb, 0x0cff, 0x0d03, 0x0d06, 0x0d0a, 0x0d0e, 0x0d11, 0x0d15, 0x0d18, 0x0d1c, 0x0d20, 0x0d23, 0x0d27,
        0x0d2a, 0x0d2e, 0x0d32, 0x0d35, 0x0d39, 0x0d3c, 0x0d40, 0x0d43, 0x0d47, 0x0d4a, 0x0d4e, 0x0d51, 0x0d55, 0x0d58, 0x0d5c,
        0x0d5f, 0x0d62, 0x0d66, 0x0d69, 0x0d6d, 0x0d70, 0x0d74, 0x0d77, 0x0d7a, 0x0d7e, 0x0d81, 0x0d85, 0x0d88, 0x0d8b, 0x0d8f,
        0x0d92, 0x0d95, 0x0d99, 0x0d9c, 0x0d9f, 0x0da2, 0x0da6, 0x0da9, 0x0dac, 0x0db0, 0x0db3, 0x0db6, 0x0db9, 0x0dbc, 0x0dc0,
        0x0dc3, 0x0dc6, 0x0dc9, 0x0dcc, 0x0dd0, 0x0dd3, 0x0dd6, 0x0dd9, 0x0ddc, 0x0ddf, 0x0de3, 0x0de6, 0x0de9, 0x0dec, 0x0def,
        0x0df2, 0x0df5, 0x0df8, 0x0dfb, 0x0dfe, 0x0e01, 0x0e04, 0x0e07, 0x0e0a, 0x0e0d, 0x0e10, 0x0e13, 0x0e16, 0x0e19, 0x0e1c,
        0x0e1f, 0x0e22, 0x0e25, 0x0e28, 0x0e2b, 0x0e2e, 0x0e31, 0x0e34, 0x0e37, 0x0e3a, 0x0e3c, 0x0e3f, 0x0e42, 0x0e45, 0x0e48,
        0x0e4b, 0x0e4d, 0x0e50, 0x0e53, 0x0e56, 0x0e59, 0x0e5b, 0x0e5e, 0x0e61, 0x0e64, 0x0e66, 0x0e69, 0x0e6c, 0x0e6f, 0x0e71,
        0x0e74, 0x0e77, 0x0e79, 0x0e7c, 0x0e7f, 0x0e81, 0x0e84, 0x0e87, 0x0e89, 0x0e8c, 0x0e8f, 0x0e91, 0x0e94, 0x0e96, 0x0e99,
        0x0e9b, 0x0e9e, 0x0ea1, 0x0ea3, 0x0ea6, 0x0ea8, 0x0eab, 0x0ead, 0x0eb0, 0x0eb2, 0x0eb5, 0x0eb7, 0x0eba, 0x0ebc, 0x0ebf,
        0x0ec1, 0x0ec3, 0x0ec6, 0x0ec8, 0x0ecb, 0x0ecd, 0x0ecf, 0x0ed2, 0x0ed4, 0x0ed6, 0x0ed9, 0x0edb, 0x0edd, 0x0ee0, 0x0ee2,
        0x0ee4, 0x0ee7, 0x0ee9, 0x0eeb, 0x0eee, 0x0ef0, 0x0ef2, 0x0ef4, 0x0ef7, 0x0ef9, 0x0efb, 0x0efd, 0x0eff, 0x0f02, 0x0f04,
        0x0f06, 0x0f08, 0x0f0a, 0x0f0c, 0x0f0e, 0x0f11, 0x0f13, 0x0f15, 0x0f17, 0x0f19, 0x0f1b, 0x0f1d, 0x0f1f, 0x0f21, 0x0f23,
        0x0f25, 0x0f27, 0x0f29, 0x0f2b, 0x0f2d, 0x0f2f, 0x0f31, 0x0f33, 0x0f35, 0x0f37, 0x0f39, 0x0f3b, 0x0f3d, 0x0f3f, 0x0f41,
        0x0f43, 0x0f45, 0x0f46, 0x0f48, 0x0f4a, 0x0f4c, 0x0f4e, 0x0f50, 0x0f51, 0x0f53, 0x0f55, 0x0f57, 0x0f59, 0x0f5a, 0x0f5c,
        0x0f5e, 0x0f60, 0x0f61, 0x0f63, 0x0f65, 0x0f67, 0x0f68, 0x0f6a, 0x0f6c, 0x0f6d, 0x0f6f, 0x0f71, 0x0f72, 0x0f74, 0x0f76,
        0x0f77, 0x0f79, 0x0f7a, 0x0f7c, 0x0f7d, 0x0f7f, 0x0f81, 0x0f82, 0x0f84, 0x0f85, 0x0f87, 0x0f88, 0x0f8a, 0x0f8b, 0x0f8d,
        0x0f8e, 0x0f90, 0x0f91, 0x0f93, 0x0f94, 0x0f95, 0x0f97, 0x0f98, 0x0f9a, 0x0f9b, 0x0f9c, 0x0f9e, 0x0f9f, 0x0fa1, 0x0fa2,
        0x0fa3, 0x0fa5, 0x0fa6, 0x0fa7, 0x0fa8, 0x0faa, 0x0fab, 0x0fac, 0x0fae, 0x0faf, 0x0fb0, 0x0fb1, 0x0fb3, 0x0fb4, 0x0fb5,
        0x0fb6, 0x0fb7, 0x0fb8, 0x0fba, 0x0fbb, 0x0fbc, 0x0fbd, 0x0fbe, 0x0fbf, 0x0fc0, 0x0fc2, 0x0fc3, 0x0fc4, 0x0fc5, 0x0fc6,
        0x0fc7, 0x0fc8, 0x0fc9, 0x0fca, 0x0fcb, 0x0fcc, 0x0fcd, 0x0fce, 0x0fcf, 0x0fd0, 0x0fd1, 0x0fd2, 0x0fd3, 0x0fd4, 0x0fd5,
        0x0fd5, 0x0fd6, 0x0fd7, 0x0fd8, 0x0fd9, 0x0fda, 0x0fdb, 0x0fdc, 0x0fdc, 0x0fdd, 0x0fde, 0x0fdf, 0x0fe0, 0x0fe0, 0x0fe1,
        0x0fe2, 0x0fe3, 0x0fe3, 0x0fe4, 0x0fe5, 0x0fe6, 0x0fe6, 0x0fe7, 0x0fe8, 0x0fe8, 0x0fe9, 0x0fea, 0x0fea, 0x0feb, 0x0fec,
        0x0fec, 0x0fed, 0x0fed, 0x0fee, 0x0fef, 0x0fef, 0x0ff0, 0x0ff0, 0x0ff1, 0x0ff1, 0x0ff2, 0x0ff2, 0x0ff3, 0x0ff3, 0x0ff4,
        0x0ff4, 0x0ff5, 0x0ff5, 0x0ff6, 0x0ff6, 0x0ff7, 0x0ff7, 0x0ff8, 0x0ff8, 0x0ff8, 0x0ff9, 0x0ff9, 0x0ff9, 0x0ffa, 0x0ffa,
        0x0ffa, 0x0ffb, 0x0ffb, 0x0ffb, 0x0ffc, 0x0ffc, 0x0ffc, 0x0ffc, 0x0ffd, 0x0ffd, 0x0ffd, 0x0ffd, 0x0ffe, 0x0ffe, 0x0ffe,
        0x0ffe, 0x0ffe, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
        0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
        0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0ffe, 0x0ffe, 0x0ffe, 0x0ffe, 0x0ffe, 0x0ffd, 0x0ffd, 0x0ffd,
        0x0ffd, 0x0ffc, 0x0ffc, 0x0ffc, 0x0ffc, 0x0ffb, 0x0ffb, 0x0ffb, 0x0ffa, 0x0ffa, 0x0ffa, 0x0ff9, 0x0ff9, 0x0ff9, 0x0ff8,
        0x0ff8, 0x0ff8, 0x0ff7, 0x0ff7, 0x0ff6, 0x0ff6, 0x0ff5, 0x0ff5, 0x0ff4, 0x0ff4, 0x0ff3, 0x0ff3, 0x0ff2, 0x0ff2, 0x0ff1,
        0x0ff1, 0x0ff0, 0x0ff0, 0x0fef, 0x0fef, 0x0fee, 0x0fed, 0x0fed, 0x0fec, 0x0fec, 0x0feb, 0x0fea, 0x0fea, 0x0fe9, 0x0fe8,
        0x0fe8, 0x0fe7, 0x0fe6, 0x0fe6, 0x0fe5, 0x0fe4, 0x0fe3, 0x0fe3, 0x0fe2, 0x0fe1, 0x0fe0, 0x0fe0, 0x0fdf, 0x0fde, 0x0fdd,
        0x0fdc, 0x0fdc, 0x0fdb, 0x0fda, 0x0fd9, 0x0fd8, 0x0fd7, 0x0fd6, 0x0fd5, 0x0fd5, 0x0fd4, 0x0fd3, 0x0fd2, 0x0fd1, 0x0fd0,
        0x0fcf, 0x0fce, 0x0fcd, 0x0fcc, 0x0fcb, 0x0fca, 0x0fc9, 0x0fc8, 0x0fc7, 0x0fc6, 0x0fc5, 0x0fc4, 0x0fc3, 0x0fc2, 0x0fc0,
        0x0fbf, 0x0fbe, 0x0fbd, 0x0fbc, 0x0fbb, 0x0fba, 0x0fb8, 0x0fb7, 0x0fb6, 0x0fb5, 0x0fb4, 0x0fb3, 0x0fb1, 0x0fb0, 0x0faf,
        0x0fae, 0x0fac, 0x0fab, 0x0faa, 0x0fa8, 0x0fa7, 0x0fa6, 0x0fa5, 0x0fa3, 0x0fa2, 0x0fa1, 0x0f9f, 0x0f9e, 0x0f9c, 0x0f9b,
        0x0f9a, 0x0f98, 0x0f97, 0x0f95, 0x0f94, 0x0f93, 0x0f91, 0x0f90, 0x0f8e, 0x0f8d, 0x0f8b, 0x0f8a, 0x0f88, 0x0f87, 0x0f85,
        0x0f84, 0x0f82, 0x0f81, 0x0f7f, 0x0f7d, 0x0f7c, 0x0f7a, 0x0f79, 0x0f77, 0x0f76, 0x0f74, 0x0f72, 0x0f71, 0x0f6f, 0x0f6d,
        0x0f6c, 0x0f6a, 0x0f68, 0x0f67, 0x0f65, 0x0f63, 0x0f61, 0x0f60, 0x0f5e, 0x0f5c, 0x0f5a, 0x0f59, 0x0f57, 0x0f55, 0x0f53,
        0x0f51, 0x0f50, 0x0f4e, 0x0f4c, 0x0f4a, 0x0f48, 0x0f46, 0x0f45, 0x0f43, 0x0f41, 0x0f3f, 0x0f3d, 0x0f3b, 0x0f39, 0x0f37,
        0x0f35, 0x0f33, 0x0f31, 0x0f2f, 0x0f2d, 0x0f2b, 0x0f29, 0x0f27, 0x0f25, 0x0f23, 0x0f21, 0x0f1f, 0x0f1d, 0x0f1b, 0x0f19,
        0x0f17, 0x0f15, 0x0f13, 0x0f11, 0x0f0e, 0x0f0c, 0x0f0a, 0x0f08, 0x0f06, 0x0f04, 0x0f02, 0x0eff, 0x0efd, 0x0efb, 0x0ef9,
        0x0ef7, 0x0ef4, 0x0ef2, 0x0ef0, 0x0eee, 0x0eeb, 0x0ee9, 0x0ee7, 0x0ee4, 0x0ee2, 0x0ee0, 0x0edd, 0x0edb, 0x0ed9, 0x0ed6,
        0x0ed4, 0x0ed2, 0x0ecf, 0x0ecd, 0x0ecb, 0x0ec8, 0x0ec6, 0x0ec3, 0x0ec1, 0x0ebf, 0x0ebc, 0x0eba, 0x0eb7, 0x0eb5, 0x0eb2,
        0x0eb0, 0x0ead, 0x0eab, 0x0ea8, 0x0ea6, 0x0ea3, 0x0ea1, 0x0e9e, 0x0e9b, 0x0e99, 0x0e96, 0x0e94, 0x0e91, 0x0e8f, 0x0e8c,
        0x0e89, 0x0e87, 0x0e84, 0x0e81, 0x0e7f, 0x0e7c, 0x0e79, 0x0e77, 0x0e74, 0x0e71, 0x0e6f, 0x0e6c, 0x0e69, 0x0e66, 0x0e64,
        0x0e61, 0x0e5e, 0x0e5b, 0x0e59, 0x0e56, 0x0e53, 0x0e50, 0x0e4d, 0x0e4b, 0x0e48, 0x0e45, 0x0e42, 0x0e3f, 0x0e3c, 0x0e3a,
        0x0e37, 0x0e34, 0x0e31, 0x0e2e, 0x0e2b, 0x0e28, 0x0e25, 0x0e22, 0x0e1f, 0x0e1c, 0x0e19, 0x0e16, 0x0e13, 0x0e10, 0x0e0d,
        0x0e0a, 0x0e07, 0x0e04, 0x0e01, 0x0dfe, 0x0dfb, 0x0df8, 0x0df5, 0x0df2, 0x0def, 0x0dec, 0x0de9, 0x0de6, 0x0de3, 0x0ddf,
        0x0ddc, 0x0dd9, 0x0dd6, 0x0dd3, 0x0dd0, 0x0dcc, 0x0dc9, 0x0dc6, 0x0dc3, 0x0dc0, 0x0dbc, 0x0db9, 0x0db6, 0x0db3, 0x0db0,
        0x0dac, 0x0da9, 0x0da6, 0x0da2, 0x0d9f, 0x0d9c, 0x0d99, 0x0d95, 0x0d92, 0x0d8f, 0x0d8b, 0x0d88, 0x0d85, 0x0d81, 0x0d7e,
        0x0d7a, 0x0d77, 0x0d74, 0x0d70, 0x0d6d, 0x0d69, 0x0d66, 0x0d62, 0x0d5f, 0x0d5c, 0x0d58, 0x0d55, 0x0d51, 0x0d4e, 0x0d4a,
        0x0d47, 0x0d43, 0x0d40, 0x0d3c, 0x0d39, 0x0d35, 0x0d32, 0x0d2e, 0x0d2a, 0x0d27, 0x0d23, 0x0d20, 0x0d1c, 0x0d18, 0x0d15,
        0x0d11, 0x0d0e, 0x0d0a, 0x0d06, 0x0d03, 0x0cff, 0x0cfb, 0x0cf8, 0x0cf4, 0x0cf0, 0x0ced, 0x0ce9, 0x0ce5, 0x0ce1, 0x0cde,
        0x0cda, 0x0cd6, 0x0cd2, 0x0ccf, 0x0ccb, 0x0cc7, 0x0cc3, 0x0cc0, 0x0cbc, 0x0cb8, 0x0cb4, 0x0cb0, 0x0cac, 0x0ca9, 0x0ca5,
        0x0ca1, 0x0c9d, 0x0c99, 0x0c95, 0x0c91, 0x0c8e, 0x0c8a, 0x0c86, 0x0c82, 0x0c7e, 0x0c7a, 0x0c76, 0x0c72, 0x0c6e, 0x0c6a,
        0x0c66, 0x0c62, 0x0c5e, 0x0c5a, 0x0c56, 0x0c52, 0x0c4e, 0x0c4a, 0x0c46, 0x0c42, 0x0c3e, 0x0c3a, 0x0c36, 0x0c32, 0x0c2e,
        0x0c2a, 0x0c26, 0x0c22, 0x0c1e, 0x0c19, 0x0c15, 0x0c11, 0x0c0d, 0x0c09, 0x0c05, 0x0c01, 0x0bfc, 0x0bf8, 0x0bf4, 0x0bf0,
        0x0bec, 0x0be8, 0x0be3, 0x0bdf, 0x0bdb, 0x0bd7, 0x0bd2, 0x0bce, 0x0bca, 0x0bc6, 0x0bc1, 0x0bbd, 0x0bb9, 0x0bb5, 0x0bb0,
        0x0bac, 0x0ba8, 0x0ba3, 0x0b9f, 0x0b9b, 0x0b97, 0x0b92, 0x0b8e, 0x0b89, 0x0b85, 0x0b81, 0x0b7c, 0x0b78, 0x0b74, 0x0b6f,
        0x0b6b, 0x0b66, 0x0b62, 0x0b5e, 0x0b59, 0x0b55, 0x0b50, 0x0b4c, 0x0b47, 0x0b43, 0x0b3e, 0x0b3a, 0x0b36, 0x0b31, 0x0b2d,
        0x0b28, 0x0b24, 0x0b1f, 0x0b1b, 0x0b16, 0x0b11, 0x0b0d, 0x0b08, 0x0b04, 0x0aff, 0x0afb, 0x0af6, 0x0af2, 0x0aed, 0x0ae8,
        0x0ae4, 0x0adf, 0x0adb, 0x0ad6, 0x0ad1, 0x0acd, 0x0ac8, 0x0ac3, 0x0abf, 0x0aba, 0x0ab5, 0x0ab1, 0x0aac, 0x0aa7, 0x0aa3,
        0x0a9e, 0x0a99, 0x0a95, 0x0a90, 0x0a8b, 0x0a86, 0x0a82, 0x0a7d, 0x0a78, 0x0a73, 0x0a6f, 0x0a6a, 0x0a65, 0x0a60, 0x0a5c,
        0x0a57, 0x0a52, 0x0a4d, 0x0a48, 0x0a44, 0x0a3f, 0x0a3a, 0x0a35, 0x0a30, 0x0a2b, 0x0a26, 0x0a22, 0x0a1d, 0x0a18, 0x0a13,
        0x0a0e, 0x0a09, 0x0a04, 0x09ff, 0x09fb, 0x09f6, 0x09f1, 0x09ec, 0x09e7, 0x09e2, 0x09dd, 0x09d8, 0x09d3, 0x09ce, 0x09c9,
        0x09c4, 0x09bf, 0x09ba, 0x09b5, 0x09b0, 0x09ab, 0x09a6, 0x09a1, 0x099c, 0x0997, 0x0992, 0x098d, 0x0988, 0x0983, 0x097e,
        0x0979, 0x0974, 0x096f, 0x096a, 0x0965, 0x095f, 0x095a, 0x0955, 0x0950, 0x094b, 0x0946, 0x0941, 0x093c, 0x0937, 0x0931,
        0x092c, 0x0927, 0x0922, 0x091d, 0x0918, 0x0912, 0x090d, 0x0908, 0x0903, 0x08fe, 0x08f8, 0x08f3, 0x08ee, 0x08e9, 0x08e4,
        0x08de, 0x08d9, 0x08d4, 0x08cf, 0x08c9, 0x08c4, 0x08bf, 0x08ba, 0x08b4, 0x08af, 0x08aa, 0x08a5, 0x089f, 0x089a, 0x0895,
        0x088f, 0x088a, 0x0885, 0x087f, 0x087a, 0x0875, 0x086f, 0x086a, 0x0865, 0x085f, 0x085a, 0x0855, 0x084f, 0x084a, 0x0845,
        0x083f, 0x083a, 0x0834, 0x082f, 0x082a, 0x0824, 0x081f, 0x0819, 0x0814, 0x080e, 0x0809, 0x0804, 0x07fe, 0x07f9, 0x07f3,
        0x07ee, 0x07e8, 0x07e3, 0x07dd, 0x07d8, 0x07d2, 0x07cd, 0x07c8, 0x07c2, 0x07bd, 0x07b7, 0x07b2, 0x07ac, 0x07a6, 0x07a1,
        0x079b, 0x0796, 0x0790, 0x078b, 0x0785, 0x0780, 0x077a, 0x0775, 0x076f, 0x076a, 0x0764, 0x075e, 0x0759, 0x0753, 0x074e,
        0x0748, 0x0742, 0x073d, 0x0737, 0x0732, 0x072c, 0x0726, 0x0721, 0x071b, 0x0715, 0x0710, 0x070a, 0x0705, 0x06ff, 0x06f9,
        0x06f4, 0x06ee, 0x06e8, 0x06e3, 0x06dd, 0x06d7, 0x06d2, 0x06cc, 0x06c6, 0x06c1, 0x06bb, 0x06b5, 0x06af, 0x06aa, 0x06a4,
        0x069e, 0x0699, 0x0693, 0x068d, 0x0687, 0x0682, 0x067c, 0x0676, 0x0670, 0x066b, 0x0665, 0x065f, 0x0659, 0x0654, 0x064e,
        0x0648, 0x0642, 0x063c, 0x0637, 0x0631, 0x062b, 0x0625, 0x061f, 0x061a, 0x0614, 0x060e, 0x0608, 0x0602, 0x05fd, 0x05f7,
        0x05f1, 0x05eb, 0x05e5, 0x05df, 0x05da, 0x05d4, 0x05ce, 0x05c8, 0x05c2, 0x05bc, 0x05b6, 0x05b1, 0x05ab, 0x05a5, 0x059f,
        0x0599, 0x0593, 0x058d, 0x0587, 0x0581, 0x057c, 0x0576, 0x0570, 0x056a, 0x0564, 0x055e, 0x0558, 0x0552, 0x054c, 0x0546,
        0x0540, 0x053a, 0x0534, 0x052f, 0x0529, 0x0523, 0x051d, 0x0517, 0x0511, 0x050b, 0x0505, 0x04ff, 0x04f9, 0x04f3, 0x04ed,
        0x04e7, 0x04e1, 0x04db, 0x04d5, 0x04cf, 0x04c9, 0x04c3, 0x04bd, 0x04b7, 0x04b1, 0x04ab, 0x04a5, 0x049f, 0x0499, 0x0493,
        0x048d, 0x0487, 0x0481, 0x047b, 0x0475, 0x046f, 0x0469, 0x0463, 0x045d, 0x0457, 0x0451, 0x044b, 0x0444, 0x043e, 0x0438,
        0x0432, 0x042c, 0x0426, 0x0420, 0x041a, 0x0414, 0x040e, 0x0408, 0x0402, 0x03fc, 0x03f6, 0x03ef, 0x03e9, 0x03e3, 0x03dd,
        0x03d7, 0x03d1, 0x03cb, 0x03c5, 0x03bf, 0x03b9, 0x03b2, 0x03ac, 0x03a6, 0x03a0, 0x039a, 0x0394, 0x038e, 0x0388, 0x0381,
        0x037b, 0x0375, 0x036f, 0x0369, 0x0363, 0x035d, 0x0356, 0x0350, 0x034a, 0x0344, 0x033e, 0x0338, 0x0332, 0x032b, 0x0325,
        0x031f, 0x0319, 0x0313, 0x030d, 0x0306, 0x0300, 0x02fa, 0x02f4, 0x02ee, 0x02e8, 0x02e1, 0x02db, 0x02d5, 0x02cf, 0x02c9,
        0x02c2, 0x02bc, 0x02b6, 0x02b0, 0x02aa, 0x02a3, 0x029d, 0x0297, 0x0291, 0x028b, 0x0284, 0x027e, 0x0278, 0x0272, 0x026c,
        0x0265, 0x025f, 0x0259, 0x0253, 0x024d, 0x0246, 0x0240, 0x023a, 0x0234, 0x022d, 0x0227, 0x0221, 0x021b, 0x0215, 0x020e,
        0x0208, 0x0202, 0x01fc, 0x01f5, 0x01ef, 0x01e9, 0x01e3, 0x01dc, 0x01d6, 0x01d0, 0x01ca, 0x01c3, 0x01bd, 0x01b7, 0x01b1,
        0x01aa, 0x01a4, 0x019e, 0x0198, 0x0191, 0x018b, 0x0185, 0x017f, 0x0178, 0x0172, 0x016c, 0x0166, 0x015f, 0x0159, 0x0153,
        0x014d, 0x0146, 0x0140, 0x013a, 0x0134, 0x012d, 0x0127, 0x0121, 0x011b, 0x0114, 0x010e, 0x0108, 0x0101, 0x00fb, 0x00f5,
        0x00ef, 0x00e8, 0x00e2, 0x00dc, 0x00d6, 0x00cf, 0x00c9, 0x00c3, 0x00bc, 0x00b6, 0x00b0, 0x00aa, 0x00a3, 0x009d, 0x0097,
        0x0090, 0x008a, 0x0084, 0x007e, 0x0077, 0x0071, 0x006b, 0x0065, 0x005e, 0x0058, 0x0052, 0x004b, 0x0045, 0x003f, 0x0039,
        0x0032, 0x002c, 0x0026, 0x001f, 0x0019, 0x0013, 0x000d, 0x0006
    };

    // 0x00451710
    static int32_t rsin(int32_t angle)
    {
        const uint32_t idx = angle & 0x7FF;
        const int32_t sign = (angle & 0x800) != 0 ? -1 : 1;
        return sign * gCosSinTable[idx];
    }

    // 0x00451760
    static int32_t rcos(int32_t angle)
    {
        return rsin(angle + 1024);
    }

    // 0x00450C20
    void mul_matrix0(Mat16& left, Mat16& right, Mat16& res)
    {
        auto left_00 = (int32_t)left.m[0];
        auto left_01 = (int32_t)left.m[1];
        auto left_02 = (int32_t)left.m[2];
        auto left_10 = (int32_t)left.m[3];
        auto left_11 = (int32_t)left.m[4];
        auto left_12 = (int32_t)left.m[5];
        auto left_20 = (int32_t)left.m[6];
        auto left_21 = (int32_t)left.m[7];
        auto left_22 = (int32_t)left.m[8];

        auto right_00 = (int32_t)right.m[0];
        auto right_01 = (int32_t)right.m[1];
        auto right_02 = (int32_t)right.m[2];
        auto right_10 = (int32_t)right.m[3];
        auto right_11 = (int32_t)right.m[4];
        auto right_12 = (int32_t)right.m[5];
        auto right_20 = (int32_t)right.m[6];
        auto right_21 = (int32_t)right.m[7];
        auto right_22 = (int32_t)right.m[8];

        // Row 1
        res.m[0] = (left_00 * right_00 + left_01 * right_10 + left_02 * right_20) >> 12;
        res.m[1] = (left_00 * right_01 + left_01 * right_11 + left_02 * right_21) >> 12;
        res.m[2] = (left_00 * right_02 + left_01 * right_12 + left_02 * right_22) >> 12;
        // Row 2
        res.m[3] = (left_10 * right_00 + left_11 * right_10 + left_12 * right_20) >> 12;
        res.m[4] = (left_10 * right_01 + left_11 * right_11 + left_12 * right_21) >> 12;
        res.m[5] = (left_10 * right_02 + left_11 * right_12 + left_12 * right_22) >> 12;
        // Row 3
        res.m[6] = (left_20 * right_00 + left_21 * right_10 + left_22 * right_20) >> 12;
        res.m[7] = (left_20 * right_01 + left_21 * right_11 + left_22 * right_21) >> 12;
        res.m[8] = (left_20 * right_02 + left_21 * right_12 + left_22 * right_22) >> 12;
    }

    // 0x00450DD0
    static void mul_matrix(Mat16& left, Mat16& right)
    {
        mul_matrix0(left, right, left);
    }

    // 0x00450DF0
    static void mul_matrix2(Mat16& left, Mat16& right)
    {
        mul_matrix0(left, right, right);
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
    void rotate_matrix(Vec16p& vAngles, Mat16& m)
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
        a1.m[0] = a1.m[0] * a2.x / 4096;
        a1.m[1] = a1.m[1] * a2.y / 4096;
        a1.m[2] = a1.m[2] * a2.z / 4096;
        // Row 2
        a1.m[3] = a1.m[3] * a2.x / 4096;
        a1.m[4] = a1.m[4] * a2.y / 4096;
        a1.m[5] = a1.m[5] * a2.z / 4096;
        // Row 3
        a1.m[6] = a1.m[6] * a2.x / 4096;
        a1.m[7] = a1.m[7] * a2.y / 4096;
        a1.m[8] = a1.m[8] * a2.z / 4096;
    }

    // 0x004E7210
    Mat16& get_matrix(uint8_t type, uint8_t id)
    {
        auto signedType = static_cast<int8_t>(type);
        if (signedType < 0)
        {
            auto v3 = (signedType >> 5) & 3;
            if (v3)
            {
                auto v4 = v3 - 1;
                if (v4 == 1)
                {
                    return gGameTable.enemies[id]->pSin_parts_ptr->workm;
                }

                return gGameTable.splayer_work->pSin_parts_ptr->workm;
            }

            return gGameTable.player_work->pSin_parts_ptr->workm;
        }

        switch (type)
        {
        case 0: return gGameTable.g_identity_mat;
        case 1: return gGameTable.player_work->m;
        case 2: return gGameTable.splayer_work->m;
        case 3: return gGameTable.enemies[id]->m;
        case 4: return gGameTable.pOm[id].workm;
        }

        return gGameTable.g_identity_mat;
    }

    // 0x00450950
    static void apply_matrix(const Mat16& m, const Vec16& v, Vec32& res)
    {
        res.x = (v.x * m.m[0] + v.y * m.m[1] + v.z * m.m[2]) >> 12;
        res.y = (v.x * m.m[3] + v.y * m.m[4] + v.z * m.m[5]) >> 12;
        res.z = (v.x * m.m[6] + v.y * m.m[7] + v.z * m.m[8]) >> 12;
    }

    // 0x00450A10
    static void apply_matrixlv(const Mat16& m, const Vec32& v, Vec32& res)
    {
        res.x = (v.x * m.m[0] + v.y * m.m[1] + v.z * m.m[2]) >> 12;
        res.y = (v.x * m.m[3] + v.y * m.m[4] + v.z * m.m[5]) >> 12;
        res.z = (v.x * m.m[6] + v.y * m.m[7] + v.z * m.m[8]) >> 12;
    }

    // 0x004509D0
    void apply_matrixsv(const Mat16& m, const Vec16& v1, Vec16& res)
    {
        Vec32 vec32;
        apply_matrix(m, v1, vec32);
        res.x = vec32.x;
        res.y = vec32.y;
        res.z = vec32.z;
    }

    // 0x00450E10
    void compare_matrix(Mat16& m1, Mat16& m2, Mat16& res)
    {
        Mat16 resVal;
        Vec32 resValT;
        const Vec32 m2_t{ m2.pos.x, m2.pos.y, m2.pos.z };

        mul_matrix0(m1, m2, resVal);
        apply_matrixlv(m1, m2_t, resValT);
        resVal.pos.x = resValT.x + m1.pos.x;
        resVal.pos.y = resValT.y + m1.pos.y;
        resVal.pos.z = resValT.z + m1.pos.z;

        memcpy(&res, &resVal, sizeof(Mat16));
    }

    // 0x004513C0
    void set_rot_matrix(const Mat16& m)
    {
        auto& rc = gGameTable.rc_matrix;
        rc.m[0] = m.m[0];
        rc.m[1] = m.m[1];
        rc.m[2] = m.m[2];
        rc.m[3] = m.m[3];
        rc.m[4] = m.m[4];
        rc.m[5] = m.m[5];
        rc.m[6] = m.m[6];
        rc.m[7] = m.m[7];
        rc.m[8] = m.m[8];
    }

    // 0x00451470
    void set_trans_matrix(const uint32_t* a1)
    {
        auto& rc = gGameTable.rc_matrix;
        rc.pos.x = a1[5];
        rc.pos.y = a1[6];
        rc.pos.z = a1[7];
    }

    // 0x00451490
    static void transpose_matrix(const Mat16& m, Mat16& res)
    {
        res.m[0] = m.m[0];
        res.m[1] = m.m[3];
        res.m[2] = m.m[6];
        res.m[3] = m.m[1];
        res.m[4] = m.m[4];
        res.m[5] = m.m[7];
        res.m[6] = m.m[2];
        res.m[7] = m.m[5];
        res.m[8] = m.m[8];
    }

    // 0x00451450
    void set_color_matrix(const Mat16& m)
    {
        memcpy(&gGameTable.lc_matrix, &m, sizeof(Mat16));
    }

    // 0x00451430
    void set_light_matrix(const Mat16& m)
    {
        memcpy(&gGameTable.ll_matrix, &m, sizeof(Mat16));
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
        interop::writeJmp(0x00450950, &apply_matrix);
        interop::writeJmp(0x00450A10, &apply_matrixlv);
        interop::writeJmp(0x004509D0, &apply_matrixsv);
        interop::writeJmp(0x00450E10, &compare_matrix);
        interop::writeJmp(0x004E7210, &get_matrix);
        interop::writeJmp(0x004513C0, &set_rot_matrix);
        interop::writeJmp(0x00451470, &set_trans_matrix);
        interop::writeJmp(0x00450C20, &mul_matrix0);
        interop::writeJmp(0x00450DD0, &mul_matrix);
        interop::writeJmp(0x00450DF0, &mul_matrix2);
        interop::writeJmp(0x00451490, &transpose_matrix);
        interop::writeJmp(0x00451450, &set_color_matrix);
        interop::writeJmp(0x00451430, &set_light_matrix);
    }
}