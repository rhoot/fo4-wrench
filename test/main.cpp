#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>

namespace Scaleform {
namespace Render {

template <typename T>
class Matrix2x4 {
    float f_m;
public:
    Matrix2x4() : f_m(3.14159265f) { }
    __declspec(dllexport) __declspec(noinline) T GetXScale() const { return f_m; }
};

}
}

int main(int argc,
         char** argv) {
    LoadLibraryA("XInput1_3");

    Scaleform::Render::Matrix2x4<float> m;
    printf("pi: %f\n", m.GetXScale());

    getchar();

    return 0;
}