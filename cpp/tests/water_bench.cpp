#include "upt_core.h"
#include <chrono>
#include <cstdio>
#include <string>
using namespace upt;
int main(){
  World s(408,97); s.rng.seed(99);
  int ST=-1,WA=-1;
  for(int i=0;i<SUBSTANCE_COUNT;++i){std::string k=SUBSTANCES[i].key; if(k=="STONE")ST=i; if(k=="WATER")WA=i;}
  for(int x=0;x<408;++x) s.create(x,96,ST);
  for(int y=30;y<=90;++y) for(int x=4;x<=403;++x) s.create(x,y,WA);
  for(int i=0;i<20;++i){s.liquidPressure();s.densities();s.forces();s.advect();}
  auto t0=std::chrono::steady_clock::now();
  for(int i=0;i<100;++i) s.liquidPressure();
  double dt=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  printf("C++  гидростатика: %.2f мс/шаг (%d частиц)\n", dt/100, s.maxUsed);
}
