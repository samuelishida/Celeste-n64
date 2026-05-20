#include <cstdio>
#include <cmath>
#include "../src/user/gameplay/physics/coll_mesh.hpp"
#include "../src/user/gameplay/physics/geom.hpp"
using namespace madeline_cube;
using namespace madeline_cube::physics;

struct Rng { uint32_t s; Rng(uint32_t seed):s(seed){}
    float next01(){s=s*1664525u+1013904223u;return(float)(s>>8)/(float)(1u<<24);}
    float range(float lo,float hi){return lo+next01()*(hi-lo);}
};

static RayHit Brute(const CollMesh& m,const Vec3& o,const Vec3& d,float mt){
    Vec3 p1={o.x+d.x*mt,o.y+d.y*mt,o.z+d.z*mt};
    RayHit best;float bt=mt+1;
    for(uint32_t ti=0;ti<m.header->triangle_count;++ti){
        auto& ct=m.triangles[ti];
        Vec3 a=DequantVert(m,m.vertices[ct.i0]),b=DequantVert(m,m.vertices[ct.i1]),c=DequantVert(m,m.vertices[ct.i2]);
        TriHit h=SegmentTriangle(o,p1,a,b,c,BackfaceCull::Ignore);
        if(h.hit&&h.t<bt){bt=h.t;best.hit=true;best.t=h.t;best.face_id=(int)ct.face_id;}
    }
    return best;
}

int main(){
    CollMesh* m=LoadCollMesh("filesystem/lvl/first-room.colmesh");
    if(!m){fprintf(stderr,"load fail\n");return 1;}
    float s=m->header->quant_scale;
    float wmn[3]={m->header->quant_origin[0]+m->header->aabb_min[0]*s,m->header->quant_origin[1]+m->header->aabb_min[1]*s,m->header->quant_origin[2]+m->header->aabb_min[2]*s};
    float wmx[3]={m->header->quant_origin[0]+m->header->aabb_max[0]*s,m->header->quant_origin[1]+m->header->aabb_max[1]*s,m->header->quant_origin[2]+m->header->aabb_max[2]*s};
    Rng rng(0xDEADBEEF);
    int fail=0,pass=0;
    for(int i=0;i<1000;++i){
        Vec3 o={rng.range(wmn[0],wmx[0]),rng.range(wmn[1],wmx[1]),rng.range(wmn[2],wmx[2])};
        Vec3 d={rng.range(-1,1),rng.range(-1,1),rng.range(-1,1)};
        RayHit bvh=RaycastMesh(*m,o,d,1.0f);
        RayHit br=Brute(*m,o,d,1.0f);
        bool ok=(bvh.hit==br.hit);
        if(ok&&bvh.hit) ok=std::fabs(bvh.t-br.t)<1e-4f;
        if(!ok){
            fprintf(stderr,"FAIL %d: bvh(hit=%d t=%.6f face=%d) brute(hit=%d t=%.6f face=%d)\n",
                i,(int)bvh.hit,bvh.t,bvh.face_id,(int)br.hit,br.t,br.face_id);
            fail++;
            if(fail>=5) break;
        } else pass++;
    }
    fprintf(stderr,"pass=%d fail=%d\n",pass,fail);
    FreeCollMesh(m);
    return fail>0?1:0;
}
