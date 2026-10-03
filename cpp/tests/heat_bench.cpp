#include "upt_core.h"
#include <chrono>
#include <cstdio>
#include <string>
using namespace upt;
int main() {
    World w(408, 97);
    int idW=-1, idM=-1;
    for (int i=0;i<SUBSTANCE_COUNT;++i){
        if (std::string(SUBSTANCES[i].key)=="WATER") idW=i;
        if (std::string(SUBSTANCES[i].key)=="METAL") idM=i;
    }
    for (int y=20;y<=90;++y) for (int x=5;x<=400;++x) w.create(x,y,idW);
    for (int x=5;x<=400;++x){ int i=w.create(x,91,idM); if(i>=0) w.tmp[i]=500.0; }
    for (auto& t : w.therm) t = AWAKE;
    for (int i=0;i<20;++i) w.heat();
    auto t0=std::chrono::steady_clock::now();
    for (int i=0;i<200;++i) w.heat();
    auto dt=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    std::printf("C++  тепло: %.3f мс/шаг (%d частиц)\n", dt/200, w.maxUsed);
}
