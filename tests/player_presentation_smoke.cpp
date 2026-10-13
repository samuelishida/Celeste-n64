#include <cassert>
#include <cmath>
#include "gameplay/render/player_presentation.hpp"
using namespace madeline_cube;
int main(){
 const Vec3 source{.6f,1.4f,.6f}; const Vec3 reduced=PlayerPresentationScale(source);
 assert(std::fabs(reduced.x-.9f)<.0001f && std::fabs(reduced.y-1.1f)<.0001f);
 const Vec3 unit=PlayerPresentationScale({1,1,1});assert(unit.x==1 && unit.y==1 && unit.z==1);
 const Vec3 land=PlayerPresentationScale({1.4f,.6f,1.4f});assert(std::fabs(land.y-.9f)<.0001f);
 // Both pivot lift and transformed bind-pose feet use the same scale.
 const float rest_min=-19, base=.07735f;
 assert(std::fabs(rest_min*base*reduced.y+(-rest_min*base)*reduced.y)<.0001f);
 assert(source.x==.6f && source.y==1.4f);
}
