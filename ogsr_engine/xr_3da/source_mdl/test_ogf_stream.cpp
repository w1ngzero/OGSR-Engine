//--------------------------------------------------------------------------------------------------
// test_ogf_stream.cpp — офлайн-валидация байтового контракта OGF-потока, который строит
// BuildSourceMeshOGFStream() (см. source_mdl_import.cpp), ровно так, как его читает
// CSkeletonX_ST::Load()/dxRender_Visual::Load() в движке OGSR.
//
//   g++ -std=c++17 -O2 -o test_ogf_stream test_ogf_stream.cpp \
//       source_mdl_vvd.cpp source_mdl_vtx.cpp source_mdl_split_mesh.cpp
//   ./test_ogf_stream <vvd> <vtx>
//
// ПРОВЕРЯЕТ:
//   * OGF_HEADER читается как ogf_header (формат_version=4, type=MT_SKELETON_GEOMDEF_ST=5,
//     shader_id=0, размер == sizeof(ogf_header)==44);
//   * OGF_VERTICES: fvf=OGF_VERTEXFORMAT_FVF_4L (5*0x12071980), vCount, stride вершин ==76;
//   * OGF_INDICES:  count, u16[];
//   * порядок чанков и их id/size-упаковка соответствуют IReader::find_chunk.
//--------------------------------------------------------------------------------------------------
#include "source_mdl_vvd.h"
#include "source_mdl_vtx.h"
#include "source_mdl_split_mesh.h"
#include "source_mdl_basis.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <cstdint>

using namespace SourceMdl;

// Локальное зеркало vertBoned4W (без движковых типов, тот же порядок/размер полей = 76 байт).
struct V4W
{
    std::uint16_t m[4];
    float P[3];
    float N[3];
    float T[3];
    float B[3];
    float w[3];
    float u, v;
};
static_assert(sizeof(V4W) == 76, "V4W must be 76 bytes");

// Локальное зеркало ogf_header.
struct OGFHDR
{
    std::uint8_t format_version;
    std::uint8_t type;
    std::uint16_t shader_id;
    float bbmin[3], bbmax[3];
    float bsp_c[3];
    float bsp_r;
};
static_assert(sizeof(OGFHDR) == 44, "OGFHDR must be 44 bytes");

enum { OGF_HEADER = 1, OGF_TEXTURE = 2, OGF_VERTICES = 3, OGF_INDICES = 4 };
static const std::uint32_t kFVF4L = 5u * 0x12071980u;
enum { MT_SKELETON_GEOMDEF_ST = 5, xrOGF_FormatVersion = 4 };

static int g_fail = 0;
static void check(bool ok, const char* m) { std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", m); if (!ok) g_fail++; }

static void Put32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    b.push_back((std::uint8_t)(v & 0xff)); b.push_back((std::uint8_t)((v >> 8) & 0xff));
    b.push_back((std::uint8_t)((v >> 16) & 0xff)); b.push_back((std::uint8_t)((v >> 24) & 0xff));
}
static void PutChunk(std::vector<std::uint8_t>& b, std::uint32_t id, const std::vector<std::uint8_t>& p)
{ Put32(b, id); Put32(b, (std::uint32_t)p.size()); b.insert(b.end(), p.begin(), p.end()); }
static void PutCString(std::vector<std::uint8_t>& b, const char* s) { while (*s) b.push_back(*(std::uint8_t*)s++); b.push_back(0); }

// Мини-парсер find_chunk (u32 id, u32 size).
static bool FindChunk(const std::uint8_t* p, std::size_t sz, std::uint32_t id, std::size_t& off, std::size_t& len)
{
    std::size_t q = 0;
    while (q + 8 <= sz)
    {
        std::uint32_t cid, csz; std::memcpy(&cid, p + q, 4); std::memcpy(&csz, p + q + 4, 4);
        if (cid == id) { off = q + 8; len = csz; return true; }
        q += 8 + csz;
    }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: %s <vvd> <vtx>\n", argv[0]); return 2; }

    auto read = [&](const char* p) {
        FILE* f = std::fopen(p, "rb"); std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        std::vector<std::uint8_t> b((size_t)sz); if (std::fread(b.data(), 1, (size_t)sz, f) != (size_t)sz) { std::fclose(f); return b; }
        std::fclose(f); return b;
    };
    auto vvb = read(argv[1]); auto vtb = read(argv[2]);

    std::vector<VVD_VERTEX> verts;
    if (ReadVvdVertices(vvb.data(), vvb.size(), verts) != EVVDResult::Ok) return 2;
    std::vector<VTX_MESH> vmeshes;
    if (ReadVtxMeshes(vtb.data(), vtb.size(), vmeshes) != EVTXResult::Ok) return 2;
    std::vector<MESH> meshes;
    if (!BuildSplitMesh(verts, vmeshes, meshes)) return 2;

    // Собираем V4W в X-Ray-конвенции (базис + веса/кости как в BuildXRayMesh).
    std::vector<V4W> vbuf;
    std::vector<std::uint16_t> ibuf;
    const Basis3 basis = GetSourceToXRayBasis();
    std::size_t base = 0;
    for (const auto& m : meshes)
    {
        for (const auto& v : m.vertices)
        {
            V4W r{};
            Vec3f p = Transform(basis, v.pos);
            Vec3f n = Transform(basis, v.normal);
            Vec3f up{0,0,1}; if (std::fabs(n.z) > 0.99f) up = Vec3f{1,0,0};
            Vec3f t{up.y*n.z-up.z*n.y, up.z*n.x-up.x*n.z, up.x*n.y-up.y*n.x};
            Vec3f bt{n.y*t.z-n.z*t.y, n.z*t.x-n.x*t.z, n.x*t.y-n.y*t.x};
            r.P[0]=p.x; r.P[1]=p.y; r.P[2]=p.z;
            r.N[0]=n.x; r.N[1]=n.y; r.N[2]=n.z;
            r.T[0]=t.x; r.T[1]=t.y; r.T[2]=t.z;
            r.B[0]=bt.x; r.B[1]=bt.y; r.B[2]=bt.z;
            r.u=v.u; r.v=v.v;
            const int nb = v.num_weights>0?v.num_weights:1;
            for (int i=0;i<4;i++){ if (i<nb && i<3){ r.m[i]=v.bone[i]; r.w[i]=v.weight[i]; } else { r.m[i]=0; r.w[i]=0; } }
            vbuf.push_back(r);
        }
        for (const auto& tri : m.triangles) { ibuf.push_back((std::uint16_t)(tri.a+base)); ibuf.push_back((std::uint16_t)(tri.b+base)); ibuf.push_back((std::uint16_t)(tri.c+base)); }
        base += m.vertices.size();
    }
    std::printf("assembled %d verts / %zu tris\n", (int)vbuf.size(), ibuf.size()/3);

    // --- строим поток как BuildSourceMeshOGFStream ---
    std::vector<std::uint8_t> ogf;
    {
        // OGF_HEADER
        OGFHDR h{}; h.format_version = xrOGF_FormatVersion; h.type = MT_SKELETON_GEOMDEF_ST; h.shader_id = 0;
        float mn[3]={1e30f,1e30f,1e30f}, mx[3]={-1e30f,-1e30f,-1e30f};
        for (auto& v : vbuf) for (int c=0;c<3;c++){ mn[c]=std::min(mn[c],v.P[c]); mx[c]=std::max(mx[c],v.P[c]); }
        std::memcpy(h.bbmin,mn,12); std::memcpy(h.bbmax,mx,12);
        h.bsp_c[0]=(mn[0]+mx[0])*.5f; h.bsp_c[1]=(mn[1]+mx[1])*.5f; h.bsp_c[2]=(mn[2]+mx[2])*.5f;
        h.bsp_r = std::sqrt((mx[0]-mn[0])*(mx[0]-mn[0])+(mx[1]-mn[1])*(mx[1]-mn[1])+(mx[2]-mn[2])*(mx[2]-mn[2]))*.5f;
        std::vector<std::uint8_t> hp(reinterpret_cast<std::uint8_t*>(&h), reinterpret_cast<std::uint8_t*>(&h)+sizeof(h));
        // OGF_TEXTURE
        std::vector<std::uint8_t> tp; PutCString(tp,"models\\hands\\c_hands"); PutCString(tp,"default");
        // OGF_VERTICES
        std::vector<std::uint8_t> vp; Put32(vp,kFVF4L); Put32(vp,(std::uint32_t)vbuf.size());
        for (auto& v : vbuf){ const std::uint8_t* q=reinterpret_cast<const std::uint8_t*>(&v); vp.insert(vp.end(),q,q+sizeof(V4W)); }
        // OGF_INDICES
        std::vector<std::uint8_t> ip; Put32(ip,(std::uint32_t)ibuf.size());
        ip.insert(ip.end(), reinterpret_cast<const std::uint8_t*>(ibuf.data()), reinterpret_cast<const std::uint8_t*>(ibuf.data())+ibuf.size()*2);
        PutChunk(ogf,OGF_HEADER,hp); PutChunk(ogf,OGF_TEXTURE,tp); PutChunk(ogf,OGF_VERTICES,vp); PutChunk(ogf,OGF_INDICES,ip);
    }
    std::printf("ogf stream = %zu bytes\n", ogf.size());

    // --- читаем как CSkeletonX_ST::Load / dxRender_Visual::Load ---
    std::size_t off,len;
    check(FindChunk(ogf.data(),ogf.size(),OGF_HEADER,off,len), "OGF_HEADER found");
    check(len==sizeof(OGFHDR), "OGF_HEADER size == 44");
    OGFHDR h; std::memcpy(&h,ogf.data()+off,44);
    check(h.format_version==xrOGF_FormatVersion, "format_version==4");
    check(h.type==MT_SKELETON_GEOMDEF_ST, "type==MT_SKELETON_GEOMDEF_ST");
    char buf[128]; std::snprintf(buf,sizeof buf,"shader_id==0 (got %u)",h.shader_id);
    check(h.shader_id==0, buf);
    std::snprintf(buf,sizeof buf,"bb x [%.1f..%.1f]",h.bbmin[0],h.bbmax[0]);
    std::printf("   %s\n", buf);

    check(FindChunk(ogf.data(),ogf.size(),OGF_VERTICES,off,len), "OGF_VERTICES found");
    std::uint32_t fvf,vc; std::memcpy(&fvf,ogf.data()+off,4); std::memcpy(&vc,ogf.data()+off+4,4);
    check(fvf==kFVF4L, "fvf==FVF_4L");
    std::snprintf(buf,sizeof buf,"vCount==%d",(int)vc); check(vc==(std::uint32_t)vbuf.size(), buf);

    std::size_t expectedV = 8 + (std::size_t)vc*sizeof(V4W);
    std::snprintf(buf,sizeof buf,"vert payload==8+vCount*76 (got %zu, expected %zu)",len,expectedV);
    check(len==expectedV, buf);

    check(FindChunk(ogf.data(),ogf.size(),OGF_INDICES,off,len), "OGF_INDICES found");
    std::uint32_t ic; std::memcpy(&ic,ogf.data()+off,4);
    std::snprintf(buf,sizeof buf,"iCount==%d",(int)ic); check(ic==(std::uint32_t)ibuf.size(), buf);
    std::size_t expectedI = 4 + (std::size_t)ic*2;
    std::snprintf(buf,sizeof buf,"idx payload==4+iCount*2 (got %zu, expected %zu)",len,expectedI);
    check(len==expectedI, buf);
    // первый индекс
    std::uint16_t i0; std::memcpy(&i0,ogf.data()+off+4,2);
    std::snprintf(buf,sizeof buf,"idx[0]==%d",(int)ibuf[0]); check(i0==ibuf[0], buf);

    if (g_fail==0) std::printf("ALL OGF STREAM CHECKS PASSED\n"); else std::printf("%d FAILURE(S)\n",g_fail);
    return g_fail?1:0;
}
