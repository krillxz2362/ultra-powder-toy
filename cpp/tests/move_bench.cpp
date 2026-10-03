#include "upt_core.h"
#include <chrono>
#include <cstdio>
#include <string>
using namespace upt;
int main() {
    World w(408, 97);
    w.rng.seed(12345);
    int sand=-1, water=-1, stone=-1;
    for (int i=0;i<SUBSTANCE_COUNT;++i){
        std::string k = SUBSTANCES[i].key;
        if (k == "SAND")  sand  = i;
        if (k == "WATER") water = i;
        if (k == "STONE") stone = i;
    }
    for (int x=0;x<408;++x) w.create(x,96,stone);
    for (int y=10;y<=60;++y) for (int x=4;x<=403;++x) w.create(x,y, ((x+y)%2==0)?sand:water);
    for (int i=0;i<20;++i){ w.densities(); w.forces(); w.advect(); }
    auto t0=std::chrono::steady_clock::now();
    for (int i=0;i<100;++i){ w.densities(); w.forces(); w.advect(); }
    auto dt=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    std::printf("C++  движение: %.2f мс/шаг (%d частиц)\n", dt/100, w.maxUsed);
}
