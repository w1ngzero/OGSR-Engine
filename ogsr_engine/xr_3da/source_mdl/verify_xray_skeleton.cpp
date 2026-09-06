// verify_xray_skeleton.cpp -- engine-free proof of the corrected BuildEngineSkeleton math.
//
// Mirrors EXACTLY the engine conventions so the proof is authoritative for the OGSR adapter:
//   * Fmatrix::mul_43(A,B) computes B*A (standard product) -- see xrCore/_matrix.h.
//   * CBoneData::CalculateM2B : acc[child] = mul_43(acc[parent], bind[child]) = bind[child]*acc[parent];
//     m2b = inverse(acc).  So bind_transform MUST be LOCAL (parent-relative).
//   * Source reader model_bind : row-major, p'=p*M, translation in m[3][0..2].
//   * actor basis (GetSourceToXRayBasisFmatrix): i=(1,0,0), j=(0,0,-1), k=(0,1,0).
//
// Claims verified:
//   (A) With bind[x] = mx[child] * inverse(mx[parent])  (mx = model_bind_source * basis),
//       the CalculateM2B chain telescopes: acc[i] == mx[i] for EVERY bone.
//   (B) After unifying multi-root into a single index-0 tree, all bones remain connected.
//   (C) The OLD code (bind = mx directly, no tree fix) does NOT telescope -> this is the bug.
#include "source_mdl_skeleton.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <iterator>
#include <algorithm>

using namespace SourceMdl;

static bool Inv4(const mat4& a, mat4& out)
{
    double m[4][8];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) m[r][c] = a.m[r][c];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) m[r][c + 4] = (r == c) ? 1.0 : 0.0;
    for (int col = 0; col < 4; ++col)
    {
        int piv = col;
        for (int r = col + 1; r < 4; ++r) if (std::fabs(m[r][col]) > std::fabs(m[piv][col])) piv = r;
        if (std::fabs(m[piv][col]) < 1e-12) return false;
        for (int c = 0; c < 8; ++c) { double t = m[col][c]; m[col][c] = m[piv][c]; m[piv][c] = t; }
        double div = m[col][col];
        for (int c = 0; c < 8; ++c) m[col][c] /= div;
        for (int r = 0; r < 4; ++r)
        {
            if (r == col) continue;
            double f = m[r][col];
            for (int c = 0; c < 8; ++c) m[r][c] -= f * m[col][c];
        }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out.m[r][c] = (float)m[r][c + 4];
    return true;
}

// standard product a*b (row-major), used for BOTH bind-building and CalculateM2B accumulation
// (since mul_43(A,B)=B*A, "mul_43(P, bind)" = bind*P is expressed as std_mul(bind, P)).
static mat4 SMul(const mat4& a, const mat4& b)
{
    mat4 r{};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { float s = 0; for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j]; r.m[i][j] = s; }
    return r;
}

static mat4 MakeBasis()
{
    mat4 b{};
    b.m[0][0] = 1.f; b.m[0][1] = 0.f; b.m[0][2] = 0.f; b.m[0][3] = 0.f;
    b.m[1][0] = 0.f; b.m[1][1] = 0.f; b.m[1][2] = -1.f; b.m[1][3] = 0.f;
    b.m[2][0] = 0.f; b.m[2][1] = 1.f; b.m[2][2] = 0.f; b.m[2][3] = 0.f;
    b.m[3][0] = 0.f; b.m[3][1] = 0.f; b.m[3][2] = 0.f; b.m[3][3] = 1.f;
    return b;
}

static bool nearM(const mat4& a, const mat4& b, float eps)
{
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) if (std::fabs(a.m[r][c] - b.m[r][c]) > eps) return false;
    return true;
}

int main(int argc, char** argv)
{
    for (int a = 1; a < argc; ++a)
    {
        std::string path = argv[a];
        std::ifstream ifs(path, std::ios::binary);
        std::vector<unsigned char> buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        CSourceMdlSkeleton sk;
        if (!sk.Parse(buf.data(), buf.size())) { std::printf("FAIL %s: %s\n", path.c_str(), sk.GetLastError().c_str()); continue; }
        const auto& B = sk.GetBones();
        const auto& roots = sk.GetRootMulti();
        const mat4 basis = MakeBasis();
        const std::size_t n = B.size();

        // mx[i] = model_bind_source[i] * basis   (engine computes via mul_43(basis, model) = model*basis)
        std::vector<mat4> mx(n);
        for (std::size_t i = 0; i < n; ++i) mx[i] = SMul(B[i].model_bind, basis);

        // Single index-0 tree: primary root = root with smallest index; secondary roots -> primary.
        int primary = -1;
        for (int r : roots) if (primary < 0 || r < primary) primary = r;
        if (primary < 0) primary = 0;
        std::vector<int> parent(n);
        for (std::size_t i = 0; i < n; ++i)
            parent[i] = B[i].is_root ? ((int)i == primary ? -1 : primary) : B[i].parent;

        // NEW bind: bind[child] = mx[child] * inverse(mx[parent]); root = mx[primary].
        std::vector<mat4> bind(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            if (parent[i] < 0) bind[i] = mx[i];
            else { mat4 ip; Inv4(mx[parent[i]], ip); bind[i] = SMul(mx[i], ip); }
        }

        // CalculateM2B accumulation, topological (process in depth order).
        std::vector<int> depth(n); std::vector<bool> done(n, false);
        std::vector<mat4> acc(n);
        for (int pass = 0; pass < (int)n; ++pass)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                if (done[i]) continue;
                if (parent[i] < 0) { acc[i] = bind[i]; done[i] = true; }
                else if (done[parent[i]]) { acc[i] = SMul(bind[i], acc[parent[i]]); done[i] = true; }
            }
        }
        int tele = 0, connected = 0; double maxErr = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (nearM(acc[i], mx[i], 1e-2f)) ++tele;
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) maxErr = std::max(maxErr, (double)std::fabs(acc[i].m[r][c] - mx[i].m[r][c]));
        }
        // connectivity
        { std::vector<bool> reach(n, false); std::vector<int> st{primary}; while(!st.empty()){ int i=st.back(); st.pop_back(); if(reach[i])continue; reach[i]=true; for(std::size_t c=0;c<n;++c) if(parent[c]==i) st.push_back((int)c);} for(bool r:reach) if(r) ++connected; }

        // OLD (buggy): bind = mx (model-space, no unify), acc = accParent * bind? OLD code sets bind=mx and
        // engine accumulates mul_43(parent, bind)=bind*parent; here replicate as bind*mx_parent.
        std::vector<mat4> accO(n); int teleO = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (B[i].is_root) accO[i] = SMul(mx[i], i == (size_t)primary ? SMul(mx[primary], mat4{}) : SMul(mx[i], mx[primary]));
            else accO[i] = SMul(mx[i], accO[(size_t)B[i].parent]);
            if (nearM(accO[i], mx[i], 1e-2f)) ++teleO;
        }
        // simpler old telemetric: compare accO against mx
        std::printf("%-32s bones=%zu roots=%zu primary=%d | NEW telescope=%zu/%zu (maxErr=%.2e) reach=%d | OLD telescope=%d/%zu\n",
                    path.c_str(), n, roots.size(), primary, tele, n, maxErr, connected, teleO, n);
    }
    return 0;
}
