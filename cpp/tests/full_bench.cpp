// Замер полного шага на той же сцене, что и prof.lua: 408x97.
#include "upt_core.h"
#include <chrono>
#include <cstdio>
#include <string>
using namespace upt;
static int ID(const char*k){for(int i=0;i<SUBSTANCE_COUNT;++i) if(std::string(SUBSTANCES[i].key)==k) return i; return -1;}
int main(){
  const int W=408,H=97;
  World s(W,H); s.rng.seed(7);
  const int STONE=ID("STONE"),WOOD=ID("WOOD"),FIRE=ID("FIRE"),WATER=ID("WATER"),SAND=ID("SAND");
  for(int x=0;x<W;++x) s.create(x,H-1,STONE);
  for(int y=20;y<=60;++y) for(int x=4;x<=200;++x) s.create(x,y,WATER);
  for(int y=20;y<=60;++y) for(int x=205;x<=400;++x) s.create(x,y,SAND);
  for(int y=70;y<=90;++y) for(int x=100;x<=300;++x) s.create(x,y,WOOD);
  for(int x=120;x<=180;++x) s.create(x,69,FIRE);
  for(int i=0;i<30;++i) s.step();
  auto t0=std::chrono::steady_clock::now();
  for(int i=0;i<100;++i) s.step();
  double dt=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  printf("C++  полный шаг: %.2f мс (%d частиц)\n", dt/100, s.count);
}
