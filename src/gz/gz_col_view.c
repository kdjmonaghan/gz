#include <stdlib.h>
#include <math.h>
#include <n64.h>
#include <vector/vector.h>
#include <stdint.h>
#include "geometry.h"
#include "gfx.h"
#include "gu.h"
#include "gz.h"
#include "settings.h"
#include "ucode.h"
#include "util.h"
#include "z64.h"
#include "zu.h"

/* blend primitive and shading color, output environment alpha */
#define G_CC_MODULATERGB_PRIM_ENVA  PRIMITIVE, 0, SHADE, 0, \
                                    0,         0, 0,     ENVIRONMENT
/* output primitive color, output environment alpha */
#define G_CC_PRIMITIVE_ENVA         0,         0, 0,     PRIMITIVE, \
                                    0,         0, 0,     ENVIRONMENT

struct line
{
  int va;
  int vb;
};

static void vtxn_f2l(Vtx *r, z64_xyzf_t *v)
{
  *r = gdSPDefVtxN(floorf(0.5f + v->x * 128.f),
                   floorf(0.5f + v->y * 128.f),
                   floorf(0.5f + v->z * 128.f),
                   0, 0,
                   v->x * 127.f, v->y * 127.f, v->z * 127.f,
                   0xFF);
}

static void draw_line(Gfx **p_gfx_p, Gfx **p_gfx_d,
                     z64_xyz_t *v1, z64_xyz_t *v2)
{
  Vtx v[2] =
  {
    gdSPDefVtxC(v1->x, v1->y, v1->z, 0, 0, 0x00, 0x00, 0x00, 0xFF),
    gdSPDefVtxC(v2->x, v2->y, v2->z, 0, 0, 0x00, 0x00, 0x00, 0xFF),
  };

  gSPVertex((*p_gfx_p)++, gDisplayListData(p_gfx_d, v), 2, 0);
  gSPLine3D((*p_gfx_p)++, 0, 1, 0);
}

static void tri_norm(z64_xyzf_t *v1, z64_xyzf_t *v2, z64_xyzf_t *v3,
                     z64_xyzf_t *norm)
{
  norm->x = (v2->y - v1->y) * (v3->z - v1->z) -
            (v2->z - v1->z) * (v3->y - v1->y);
  norm->y = (v2->z - v1->z) * (v3->x - v1->x) -
            (v2->x - v1->x) * (v3->z - v1->z);
  norm->z = (v2->x - v1->x) * (v3->y - v1->y) -
            (v2->y - v1->y) * (v3->x - v1->x);
  float norm_d = sqrtf(norm->x * norm->x +
                       norm->y * norm->y +
                       norm->z * norm->z);
  if (norm_d != 0.f) {
    norm->x *= 127.f / norm_d;
    norm->y *= 127.f / norm_d;
    norm->z *= 127.f / norm_d;
  }
}

static void draw_tri(Gfx **p_gfx_p, Gfx **p_gfx_d,
                     z64_xyzf_t *v1, z64_xyzf_t *v2, z64_xyzf_t *v3)
{
  z64_xyzf_t norm;
  tri_norm(v1, v2, v3, &norm);

  Vtx v[3] =
  {
    gdSPDefVtxN(v1->x, v1->y, v1->z, 0, 0, norm.x, norm.y, norm.z, 0xFF),
    gdSPDefVtxN(v2->x, v2->y, v2->z, 0, 0, norm.x, norm.y, norm.z, 0xFF),
    gdSPDefVtxN(v3->x, v3->y, v3->z, 0, 0, norm.x, norm.y, norm.z, 0xFF),
  };

  gSPVertex((*p_gfx_p)++, gDisplayListData(p_gfx_d, v), 3, 0);
  gSP1Triangle((*p_gfx_p)++, 0, 1, 2, 0);
}

static void draw_quad(Gfx **p_gfx_p, Gfx **p_gfx_d,
                      z64_xyzf_t *v1, z64_xyzf_t *v2,
                      z64_xyzf_t *v3, z64_xyzf_t *v4)
{
  z64_xyzf_t norm;
  tri_norm(v1, v2, v4, &norm);

  Vtx v[4] =
  {
    gdSPDefVtxN(v1->x, v1->y, v1->z, 0, 0, norm.x, norm.y, norm.z, 0xFF),
    gdSPDefVtxN(v2->x, v2->y, v2->z, 0, 0, norm.x, norm.y, norm.z, 0xFF),
    gdSPDefVtxN(v3->x, v3->y, v3->z, 0, 0, norm.x, norm.y, norm.z, 0xFF),
    gdSPDefVtxN(v4->x, v4->y, v4->z, 0, 0, norm.x, norm.y, norm.z, 0xFF),
  };

  gSPVertex((*p_gfx_p)++, gDisplayListData(p_gfx_d, v), 4, 0);
  gSP2Triangles((*p_gfx_p)++, 0, 1, 2, 0, 0, 2, 3, 0);
}

static void draw_cyl(Gfx **p_gfx_p, Gfx **p_gfx_d,
                     float x, float y, float z, int radius, int height)
{
  static Gfx *p_cyl_gfx = NULL;

  if (!p_cyl_gfx) {
#define CYL_DIVS 12
    static Gfx cyl_gfx[5 + CYL_DIVS * 2];
    static Vtx cyl_vtx[2 + CYL_DIVS * 2];

    p_cyl_gfx = cyl_gfx;
    Gfx *cyl_gfx_p = p_cyl_gfx;

    cyl_vtx[0] = gdSPDefVtxN(0, 0,   0, 0, 0, 0, -127, 0, 0xFF);
    cyl_vtx[1] = gdSPDefVtxN(0, 128, 0, 0, 0, 0, 127,  0, 0xFF);
    for (int i = 0; i < CYL_DIVS; ++i) {
      int vtx_x = floorf(0.5f + cosf(2.f * M_PI * i / CYL_DIVS) * 128.f);
      int vtx_z = floorf(0.5f - sinf(2.f * M_PI * i / CYL_DIVS) * 128.f);
      int norm_x = cosf(2.f * M_PI * i / CYL_DIVS) * 127.f;
      int norm_z = -sinf(2.f * M_PI * i / CYL_DIVS) * 127.f;
      cyl_vtx[2 + i * 2 + 0] = gdSPDefVtxN(vtx_x, 0,   vtx_z, 0, 0,
                                           norm_x, 0, norm_z, 0xFF);
      cyl_vtx[2 + i * 2 + 1] = gdSPDefVtxN(vtx_x, 128, vtx_z, 0, 0,
                                           norm_x, 0, norm_z, 0xFF);
    }

    gSPSetGeometryMode(cyl_gfx_p++, G_CULL_BACK | G_SHADING_SMOOTH);

    gSPVertex(cyl_gfx_p++, cyl_vtx, 2 + CYL_DIVS * 2, 0);
    for (int i = 0; i < CYL_DIVS; ++i) {
      int p = (i + CYL_DIVS - 1) % CYL_DIVS;
      int v[4] =
      {
        2 + p * 2 + 0,
        2 + i * 2 + 0,
        2 + i * 2 + 1,
        2 + p * 2 + 1,
      };
      gSP2Triangles(cyl_gfx_p++, v[0], v[1], v[2], 0, v[0], v[2], v[3], 0);
    }

    gSPClearGeometryMode(cyl_gfx_p++, G_SHADING_SMOOTH);
    for (int i = 0; i < CYL_DIVS; ++i) {
      int p = (i + CYL_DIVS - 1) % CYL_DIVS;
      int v[4] =
      {
        2 + p * 2 + 0,
        2 + i * 2 + 0,
        2 + i * 2 + 1,
        2 + p * 2 + 1,
      };
      gSP2Triangles(cyl_gfx_p++, 0, v[1], v[0], 0, 1, v[3], v[2], 0);
    }

    gSPClearGeometryMode(cyl_gfx_p++, G_CULL_BACK);
    gSPEndDisplayList(cyl_gfx_p++);
#undef CYL_DIVS
  }

  Mtx m;
  {
    MtxF mf;
    guTranslateF(&mf, x, y, z);
    MtxF ms;
    guScaleF(&ms, radius / 128.f, height / 128.f, radius / 128.f);
    guMtxCatF(&ms, &mf, &mf);
    guMtxF2L(&mf, &m);
  }

  gSPMatrix((*p_gfx_p)++, gDisplayListData(p_gfx_d, m),
            G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
  gSPDisplayList((*p_gfx_p)++, p_cyl_gfx);
  gSPPopMatrix((*p_gfx_p)++, G_MTX_MODELVIEW);
}

static void ico_sph_subdivide_edge(z64_xyzf_t *r, z64_xyzf_t *a, z64_xyzf_t *b)
{
  vec3f_add(r, a, b);
  vec3f_norm(r, r);
}

static void draw_ico_sphere(Gfx **p_gfx_p, Gfx **p_gfx_d,
                            float x, float y, float z, float radius)
{
  static Gfx *p_sph_gfx = NULL;

  if (!p_sph_gfx) {
    z64_xyzf_t vtx[42];
    int r0_n = 1,   r0_m = r0_n / 5,  r0_i = 0    + 0;
    int r1_n = 5,   r1_m = r1_n / 5,  r1_i = r0_i + r0_n;
    int r2_n = 10,  r2_m = r2_n / 5,  r2_i = r1_i + r1_n;
    int r3_n = 10,  r3_m = r3_n / 5,  r3_i = r2_i + r2_n;
    int r4_n = 10,  r4_m = r4_n / 5,  r4_i = r3_i + r3_n;
    int r5_n = 5,   r5_m = r5_n / 5,  r5_i = r4_i + r4_n;
    int r6_n = 1,   r6_m = r6_n / 5,  r6_i = r5_i + r5_n;

    vtx[r0_i + (0 * r0_m + 0) % r0_n] = (z64_xyzf_t){0.f, 1.f, 0.f};
    vtx[r6_i + (0 * r6_m + 0) % r6_n] = (z64_xyzf_t){0.f, -1.f, 0.f};
    for (int i = 0; i < 5; ++i) {
      float a_xz = 2.f * M_PI / 10.f;
      float a_y = atanf(1.f / 2.f);
      vtx[r2_i + (i * r2_m + 0) % r2_n] = (z64_xyzf_t)
      {
        cos(a_xz * (i * r2_m + 0)) * cos(a_y * 1.f),
        sin(a_y * 1.f),
        -sin(a_xz * (i * r2_m + 0)) * cos(a_y * 1.f),
      };
      vtx[r4_i + (i * r4_m + 0) % r4_n] = (z64_xyzf_t)
      {
        cos(a_xz * (i * r4_m + 1)) * cos(a_y * -1.f),
        sin(a_y * -1.f),
        -sin(a_xz * (i * r4_m + 1)) * cos(a_y * -1.f),
      };
    }
    for (int i = 0; i < 5; ++i) {
      ico_sph_subdivide_edge(&vtx[r1_i + (i * r1_m + 0) % r1_n],
                             &vtx[r0_i + (i * r0_m + 0) % r0_n],
                             &vtx[r2_i + (i * r2_m + 0) % r2_n]);
      ico_sph_subdivide_edge(&vtx[r2_i + (i * r2_m + 1) % r2_n],
                             &vtx[r2_i + (i * r2_m + 0) % r2_n],
                             &vtx[r2_i + (i * r2_m + 2) % r2_n]);
      ico_sph_subdivide_edge(&vtx[r3_i + (i * r3_m + 0) % r3_n],
                             &vtx[r2_i + (i * r2_m + 0) % r2_n],
                             &vtx[r4_i + (i * r4_m + 0) % r4_n]);
      ico_sph_subdivide_edge(&vtx[r3_i + (i * r3_m + 1) % r3_n],
                             &vtx[r4_i + (i * r4_m + 0) % r4_n],
                             &vtx[r2_i + (i * r2_m + 2) % r2_n]);
      ico_sph_subdivide_edge(&vtx[r4_i + (i * r4_m + 1) % r4_n],
                             &vtx[r4_i + (i * r4_m + 0) % r4_n],
                             &vtx[r4_i + (i * r4_m + 2) % r4_n]);
      ico_sph_subdivide_edge(&vtx[r5_i + (i * r5_m + 0) % r5_n],
                             &vtx[r4_i + (i * r4_m + 0) % r4_n],
                             &vtx[r6_i + (i * r6_m + 0) % r6_n]);
    }

    static Vtx sph_vtx[42];
    static Gfx sph_gfx[45];

    for (int i = 0; i < 42; ++i)
      vtxn_f2l(&sph_vtx[i], &vtx[i]);

    p_sph_gfx = sph_gfx;
    Gfx *sph_gfx_p = p_sph_gfx;

    gSPSetGeometryMode(sph_gfx_p++, G_CULL_BACK | G_SHADING_SMOOTH);

    gSPVertex(sph_gfx_p++, &sph_vtx[r0_i], r0_n + r1_n + r2_n + r3_n,
              r0_i - r0_i);
    r3_i -= r0_i;
    r2_i -= r0_i;
    r1_i -= r0_i;
    r0_i -= r0_i;
    for (int i = 0; i < 5; ++i) {
      int v[24] =
      {
        r0_i + (i * r0_m + 0) % r0_n,
        r1_i + (i * r1_m + 0) % r1_n,
        r1_i + (i * r1_m + 1) % r1_n,

        r1_i + (i * r1_m + 0) % r1_n,
        r2_i + (i * r2_m + 0) % r2_n,
        r2_i + (i * r2_m + 1) % r2_n,

        r1_i + (i * r1_m + 0) % r1_n,
        r2_i + (i * r2_m + 1) % r2_n,
        r1_i + (i * r1_m + 1) % r1_n,

        r1_i + (i * r1_m + 1) % r1_n,
        r2_i + (i * r2_m + 1) % r2_n,
        r2_i + (i * r2_m + 2) % r2_n,

        r2_i + (i * r2_m + 0) % r2_n,
        r3_i + (i * r3_m + 0) % r3_n,
        r2_i + (i * r2_m + 1) % r2_n,

        r2_i + (i * r2_m + 1) % r2_n,
        r3_i + (i * r3_m + 0) % r3_n,
        r3_i + (i * r3_m + 1) % r3_n,

        r2_i + (i * r2_m + 1) % r2_n,
        r3_i + (i * r3_m + 1) % r3_n,
        r2_i + (i * r2_m + 2) % r2_n,

        r2_i + (i * r2_m + 2) % r2_n,
        r3_i + (i * r3_m + 1) % r3_n,
        r3_i + (i * r3_m + 2) % r3_n,
      };
      gSP2Triangles(sph_gfx_p++,
                    v[0],   v[1],   v[2],   0,
                    v[3],   v[4],   v[5],   0);
      gSP2Triangles(sph_gfx_p++,
                    v[6],   v[7],   v[8],   0,
                    v[9],   v[10],  v[11],  0);
      gSP2Triangles(sph_gfx_p++,
                    v[12],  v[13],  v[14],  0,
                    v[15],  v[16],  v[17],  0);
      gSP2Triangles(sph_gfx_p++,
                    v[18],  v[19],  v[20],  0,
                    v[21],  v[22],  v[23],  0);
    }

    gSPVertex(sph_gfx_p++, &sph_vtx[r4_i], r4_n + r5_n + r6_n,
              r4_i - r4_i);
    r6_i -= r4_i;
    r5_i -= r4_i;
    r4_i -= r4_i;
    for (int i = 0; i < 5; ++i) {
      int v[24] =
      {
        r3_i + (i * r3_m + 1) % r3_n,
        r4_i + (i * r4_m + 0) % r4_n,
        r4_i + (i * r4_m + 1) % r4_n,

        r3_i + (i * r3_m + 1) % r3_n,
        r4_i + (i * r4_m + 1) % r4_n,
        r3_i + (i * r3_m + 2) % r3_n,

        r3_i + (i * r3_m + 2) % r3_n,
        r4_i + (i * r4_m + 1) % r4_n,
        r4_i + (i * r4_m + 2) % r4_n,

        r3_i + (i * r3_m + 2) % r3_n,
        r4_i + (i * r4_m + 2) % r4_n,
        r3_i + (i * r3_m + 3) % r3_n,

        r4_i + (i * r4_m + 0) % r4_n,
        r5_i + (i * r5_m + 0) % r5_n,
        r4_i + (i * r4_m + 1) % r4_n,

        r4_i + (i * r4_m + 1) % r4_n,
        r5_i + (i * r5_m + 0) % r5_n,
        r5_i + (i * r5_m + 1) % r5_n,

        r4_i + (i * r4_m + 1) % r4_n,
        r5_i + (i * r5_m + 1) % r5_n,
        r4_i + (i * r4_m + 2) % r4_n,

        r5_i + (i * r5_m + 0) % r5_n,
        r6_i + (i * r6_m + 0) % r6_n,
        r5_i + (i * r5_m + 1) % r5_n,
      };
      gSP2Triangles(sph_gfx_p++,
                    v[0],   v[1],   v[2],   0,
                    v[3],   v[4],   v[5],   0);
      gSP2Triangles(sph_gfx_p++,
                    v[6],   v[7],   v[8],   0,
                    v[9],   v[10],  v[11],  0);
      gSP2Triangles(sph_gfx_p++,
                    v[12],  v[13],  v[14],  0,
                    v[15],  v[16],  v[17],  0);
      gSP2Triangles(sph_gfx_p++,
                    v[18],  v[19],  v[20],  0,
                    v[21],  v[22],  v[23],  0);
    }

    gSPClearGeometryMode(sph_gfx_p++, G_CULL_BACK | G_SHADING_SMOOTH);
    gSPEndDisplayList(sph_gfx_p++);
  }

  Mtx m;
  {
    MtxF mf;
    guTranslateF(&mf, x, y, z);
    MtxF ms;
    guScaleF(&ms, radius / 128.f, radius / 128.f, radius / 128.f);
    guMtxCatF(&ms, &mf, &mf);
    guMtxF2L(&mf, &m);
  }

  gSPMatrix((*p_gfx_p)++, gDisplayListData(p_gfx_d, m),
            G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
  gSPDisplayList((*p_gfx_p)++, p_sph_gfx);
  gSPPopMatrix((*p_gfx_p)++, G_MTX_MODELVIEW);
}

typedef struct
{
  Gfx      *gfx_p;
  Gfx      *gfx_d;
  Vtx       vtx_buf[30];
  Gfx      *vtx_cmd;
  Gfx      *tri_cmd;
  int       n_vtx;
  uint32_t  last_color;
} poly_writer_t;

static void poly_writer_init(poly_writer_t *writer, Gfx *gfx_p, Gfx *gfx_d)
{
  writer->gfx_p = gfx_p;
  writer->gfx_d = gfx_d;
  writer->vtx_cmd = NULL;
  writer->tri_cmd = NULL;
  writer->n_vtx = 0;
  writer->last_color = 0x00000000;

  gDPSetPrimColor(writer->gfx_p++, 0, 0, 0x00, 0x00, 0x00, 0x00);
}

static void poly_writer_flush(poly_writer_t *writer)
{
  if (writer->vtx_cmd) {
    size_t vtx_size = sizeof(Vtx) * writer->n_vtx;

    Vtx *p_vtx = gDisplayListAlloc(&writer->gfx_d, vtx_size);
    memcpy(p_vtx, writer->vtx_buf, vtx_size);
    *writer->vtx_cmd = gsSPVertex(p_vtx, writer->n_vtx, 0);
    writer->vtx_cmd = NULL;
  }

  if (writer->n_vtx % 6 != 0)
    *writer->tri_cmd = gsSP1Triangle(writer->n_vtx - 3, writer->n_vtx - 2,
                                     writer->n_vtx - 1, 0);
  writer->tri_cmd = NULL;

  writer->n_vtx = 0;
}

static void poly_writer_finish(poly_writer_t *writer,
                               Gfx **p_gfx_p, Gfx **p_gfx_d)
{
  poly_writer_flush(writer);

  *p_gfx_p = writer->gfx_p;
  *p_gfx_d = writer->gfx_d;
}

static void poly_writer_add(poly_writer_t *writer, Vtx (*v)[3], uint32_t color)
{
  if (!writer->vtx_cmd)
    writer->vtx_cmd = writer->gfx_p++;

  memcpy(&writer->vtx_buf[writer->n_vtx], v, sizeof(*v));

  if (writer->last_color != color) {
    gDPSetPrimColor(writer->gfx_p++, 0, 0,
                    (color >> 24) & 0xFF, (color >> 16) & 0xFF,
                    (color >> 8)  & 0xFF, (color >> 0)  & 0xFF);
    if (writer->n_vtx % 6 != 0) {
      *writer->tri_cmd = gsSP1Triangle(writer->n_vtx - 3, writer->n_vtx - 2,
                                       writer->n_vtx - 1, 0);
      writer->tri_cmd = writer->gfx_p++;
      *writer->tri_cmd = gsSP1Triangle(writer->n_vtx + 0, writer->n_vtx + 1,
                                       writer->n_vtx + 2, 0);
    }
    writer->last_color = color;
  }

  if (writer->n_vtx % 6 == 0) {
    writer->tri_cmd = writer->gfx_p++;
    *writer->tri_cmd = gsSP2Triangles(writer->n_vtx + 0, writer->n_vtx + 1,
                                      writer->n_vtx + 2, 0,
                                      writer->n_vtx + 3, writer->n_vtx + 4,
                                      writer->n_vtx + 5, 0);
  }

  writer->n_vtx += 3;
  if (writer->n_vtx == 30)
    poly_writer_flush(writer);
}

typedef struct
{
  Gfx      *gfx_p;
  Gfx      *gfx_d;
  Vtx       vtx_buf[32];
  Gfx      *vtx_cmd;
  int       n_vtx;
} line_writer_t;

static void line_writer_init(line_writer_t *writer, Gfx *gfx_p, Gfx *gfx_d)
{
  writer->gfx_p = gfx_p;
  writer->gfx_d = gfx_d;
  writer->vtx_cmd = NULL;
  writer->n_vtx = 0;
}

static void line_writer_flush(line_writer_t *writer)
{
  if (writer->vtx_cmd) {
    size_t vtx_size = sizeof(Vtx) * writer->n_vtx;

    Vtx *p_vtx = gDisplayListAlloc(&writer->gfx_d, vtx_size);
    memcpy(p_vtx, writer->vtx_buf, vtx_size);
    *writer->vtx_cmd = gsSPVertex(p_vtx, writer->n_vtx, 0);
    writer->vtx_cmd = NULL;
  }

  writer->n_vtx = 0;
}

static void line_writer_finish(line_writer_t *writer,
                               Gfx **p_gfx_p, Gfx **p_gfx_d)
{
  line_writer_flush(writer);

  *p_gfx_p = writer->gfx_p;
  *p_gfx_d = writer->gfx_d;
}

static void line_writer_add(line_writer_t *writer, Vtx *v, int n_vtx)
{
  if (n_vtx <= 1)
    return;

  if (writer->n_vtx + n_vtx > 32)
    line_writer_flush(writer);

  if (!writer->vtx_cmd)
    writer->vtx_cmd = writer->gfx_p++;

  memcpy(&writer->vtx_buf[writer->n_vtx], v, sizeof(*v) * n_vtx);

  for (int i = 0; i < n_vtx; ++i)
    for (int j = i + 1; j < n_vtx; ++j)
      gSPLine3D(writer->gfx_p++, writer->n_vtx + i, writer->n_vtx + j, 0);

  writer->n_vtx += n_vtx;
}

static void do_poly_list(poly_writer_t *poly_writer,
                         line_writer_t *line_writer, struct vector *line_set,
                         z64_xyz_t *vtx_list, z64_col_poly_t *poly_list,
                         z64_col_type_t *type_list, int n_poly, _Bool rd)
{
  for (int i = 0; i < n_poly; ++i) {
    z64_col_poly_t *poly = &poly_list[i];
    z64_col_type_t *type = &type_list[poly->type];
    z64_xyz_t *va = &vtx_list[poly->va];
    z64_xyz_t *vb = &vtx_list[poly->vb];
    z64_xyz_t *vc = &vtx_list[poly->vc];

    /* generate polygon */
    if (poly_writer) {
      uint32_t color;
      _Bool skip = 0;
      if (type->flags_2.hookshot)
        color = 0x8080FFFF;
      else if (type->flags_1.interaction > 0x01)
        color = 0xC000C0FF;
      else if (type->flags_1.special == 0x0C)
        color = 0xFF0000FF;
      else if (type->flags_1.exit != 0x00 || type->flags_1.special == 0x05)
        color = 0x00FF00FF;
      else if (type->flags_1.behavior != 0 || type->flags_2.wall_damage)
        color = 0xC0FFC0FF;
      else if (type->flags_2.terrain == 0x01)
        color = 0xFFFF80FF;
      else if (rd)
        skip = 1;
      else
        color = 0xFFFFFFFF;
      if (!skip) {
        Vtx v[3] =
        {
          gdSPDefVtxN(va->x, va->y, va->z, 0, 0,
                      poly->norm.x / 0x100, poly->norm.y / 0x100,
                      poly->norm.z / 0x100, 0xFF),
          gdSPDefVtxN(vb->x, vb->y, vb->z, 0, 0,
                      poly->norm.x / 0x100, poly->norm.y / 0x100,
                      poly->norm.z / 0x100, 0xFF),
          gdSPDefVtxN(vc->x, vc->y, vc->z, 0, 0,
                      poly->norm.x / 0x100, poly->norm.y / 0x100,
                      poly->norm.z / 0x100, 0xFF),
        };
        poly_writer_add(poly_writer, &v, color);
      }
    }

    /* generate lines */
    if (line_writer) {
      struct line lab = {poly->va, poly->vb};
      struct line lbc = {poly->vb, poly->vc};
      struct line lca = {poly->vc, poly->va};
      _Bool ab = 1;
      _Bool bc = 1;
      _Bool ca = 1;
      if (line_set) {
        for (int i = 0; i < line_set->size; ++i) {
          struct line *l = vector_at(line_set, i);
          if ((l->va == lab.va && l->vb == lab.vb) ||
              (l->va == lab.vb && l->vb == lab.va))
            ab = 0;
          if ((l->va == lbc.va && l->vb == lbc.vb) ||
              (l->va == lbc.vb && l->vb == lbc.va))
            bc = 0;
          if ((l->va == lca.va && l->vb == lca.vb) ||
              (l->va == lca.vb && l->vb == lca.va))
            ca = 0;
        }
        if (ab)
          vector_push_back(line_set, 1, &lab);
        if (bc)
          vector_push_back(line_set, 1, &lbc);
        if (ca)
          vector_push_back(line_set, 1, &lca);
      }
      Vtx v[3];
      int n_vtx = 0;
      if (ab || ca)
        v[n_vtx++] = gdSPDefVtxC(va->x, va->y, va->z, 0, 0,
                                 0x00, 0x00, 0x00, 0xFF);
      if (ab || bc)
        v[n_vtx++] = gdSPDefVtxC(vb->x, vb->y, vb->z, 0, 0,
                                 0x00, 0x00, 0x00, 0xFF);
      if (bc || ca)
        v[n_vtx++] = gdSPDefVtxC(vc->x, vc->y, vc->z, 0, 0,
                                 0x00, 0x00, 0x00, 0xFF);
      line_writer_add(line_writer, v, n_vtx);
    }
  }
}

static void do_dyn_list(poly_writer_t *poly_writer,
                        line_writer_t *line_writer,
                        z64_col_hdr_t *col_hdr,
                        uint16_t list_idx, _Bool rd)
{
  z64_col_ctxt_t *col_ctxt = &z64_game.col_ctxt;

  while (list_idx != 0xFFFF) {
    z64_col_list_t *list = &col_ctxt->dyn_list[list_idx];

    do_poly_list(poly_writer, line_writer, NULL,
                 col_ctxt->dyn_vtx, &col_ctxt->dyn_poly[list->poly_idx],
                 col_hdr->type, 1, rd);

    list_idx = list->list_next;
  }
}

static void do_dyn_col(poly_writer_t *poly_writer,
                       line_writer_t *line_writer,
                       _Bool rd)
{
  z64_col_ctxt_t *col_ctxt = &z64_game.col_ctxt;

  for (int i = 0; i < 32; ++i)
    if (col_ctxt->dyn_flags[i].active) {
      z64_dyn_col_t *dyn_col = &col_ctxt->dyn_col[i];

      do_dyn_list(poly_writer, line_writer,
                  dyn_col->col_hdr, dyn_col->ceil_list_idx, rd);
      do_dyn_list(poly_writer, line_writer,
                  dyn_col->col_hdr, dyn_col->wall_list_idx, rd);
      do_dyn_list(poly_writer, line_writer,
                  dyn_col->col_hdr, dyn_col->floor_list_idx, rd);
    }
}

static void init_poly_gfx(Gfx **p_gfx_p, Gfx **p_gfx_d,
                          int mode, _Bool xlu, _Bool shade)
{
  uint32_t rm;
  uint32_t blc1;
  uint32_t blc2;
  uint8_t alpha;
  uint64_t cm;
  uint32_t gm;

  if (xlu) {
    rm = Z_CMP | IM_RD | CVG_DST_FULL | FORCE_BL;
    blc1 = GBL_c1(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA);
    blc2 = GBL_c2(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA);
    alpha = 0x80;
  }
  else {
    rm = Z_CMP | Z_UPD | IM_RD | CVG_DST_CLAMP | FORCE_BL;
    blc1 = GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1);
    blc2 = GBL_c2(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1);
    alpha = 0xFF;
  }
  if (mode == SETTINGS_COLVIEW_DECAL)
    rm |= ZMODE_DEC;
  else if (xlu)
    rm |= ZMODE_XLU;
  else
    rm |= ZMODE_OPA;
  if (shade) {
    cm = G_CC_MODE(G_CC_MODULATERGB_PRIM_ENVA, G_CC_MODULATERGB_PRIM_ENVA);
    gm = G_ZBUFFER | G_SHADE | G_LIGHTING;
  }
  else {
    cm = G_CC_MODE(G_CC_PRIMITIVE_ENVA, G_CC_PRIMITIVE_ENVA);
    gm = G_ZBUFFER;
  }

  Mtx *p_m = gDisplayListAlloc(p_gfx_d, sizeof(*p_m));
  guMtxIdent(p_m);

  gSPLoadGeometryMode((*p_gfx_p)++, gm);
  gSPTexture((*p_gfx_p)++, qu016(0.5), qu016(0.5), 0, G_TX_RENDERTILE, G_OFF);
  gSPMatrix((*p_gfx_p)++, p_m, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

  gDPPipeSync((*p_gfx_p)++);
  gDPSetCycleType((*p_gfx_p)++, G_CYC_1CYCLE);
  gDPSetRenderMode((*p_gfx_p)++, rm | blc1, rm | blc2);
  gDPSetCombine((*p_gfx_p)++, cm);
  gDPSetEnvColor((*p_gfx_p)++, 0xFF, 0xFF, 0xFF, alpha);
}

static void init_line_gfx(Gfx **p_gfx_p, Gfx **p_gfx_d, _Bool xlu)
{
  uint32_t rm_c1;
  uint32_t rm_c2;
  uint8_t alpha;

  if (xlu) {
    rm_c1 = G_RM_AA_ZB_XLU_LINE;
    rm_c2 = G_RM_AA_ZB_XLU_LINE2;
    alpha = 0x80;
  }
  else {
    rm_c1 = G_RM_AA_ZB_DEC_LINE;
    rm_c2 = G_RM_AA_ZB_DEC_LINE2;
    alpha = 0xFF;
  }

  Mtx *p_m = gDisplayListAlloc(p_gfx_d, sizeof(*p_m));
  guMtxIdent(p_m);

  gSPLoadGeometryMode((*p_gfx_p)++, G_ZBUFFER);
  gSPTexture((*p_gfx_p)++, qu016(0.5), qu016(0.5), 0, G_TX_RENDERTILE, G_OFF);
  gSPMatrix((*p_gfx_p)++, p_m, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

  gDPPipeSync((*p_gfx_p)++);
  gDPSetCycleType((*p_gfx_p)++, G_CYC_1CYCLE);
  gDPSetRenderMode((*p_gfx_p)++, rm_c1, rm_c2);
  gDPSetCombineMode((*p_gfx_p)++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
  gDPSetPrimColor((*p_gfx_p)++, 0, 0, 0x00, 0x00, 0x00, alpha);
}

static void release_mem(void *p_ptr)
{
  void **p_void = (void**)p_ptr;
  if (*p_void) {
    free(*p_void);
    *p_void = NULL;
  }
}

void gz_col_view(void)
{
  const int dyn_poly_cap = 0x1000;
  const int dyn_line_cap = 0x1000;

  static Gfx *stc_poly;
  static Gfx *stc_line;
  static Gfx *dyn_poly_buf[2];
  static Gfx *dyn_line_buf[2];
  static int  dyn_gfx_idx = 0;

  static int col_view_scene;
  static int col_view_line;
  static int col_view_rd;

  poly_writer_t poly_writer;
  line_writer_t line_writer;

  _Bool enable = zu_in_game() && z64_game.pause_ctxt.state == 0;
  _Bool init = gz.col_view_state == COLVIEW_START ||
               gz.col_view_state == COLVIEW_RESTART;

  /* restart if needed */
  if (enable && gz.col_view_state == COLVIEW_ACTIVE &&
      settings->bits.col_view_upd &&
      (col_view_scene != z64_game.scene_index ||
       col_view_line != settings->bits.col_view_line ||
       col_view_rd != settings->bits.col_view_rd))
  {
    gz.col_view_state = COLVIEW_BEGIN_RESTART;
  }

  /* update state */
  switch (gz.col_view_state) {
    case COLVIEW_BEGIN_STOP:
      gz.col_view_state = COLVIEW_STOP;
      break;

    case COLVIEW_BEGIN_RESTART:
      gz.col_view_state = COLVIEW_RESTART;
      break;

    case COLVIEW_STOP:
      gz.col_view_state = COLVIEW_INACTIVE;
    case COLVIEW_RESTART:
      release_mem(&stc_poly);
      release_mem(&stc_line);
      release_mem(&dyn_poly_buf[0]);
      release_mem(&dyn_poly_buf[1]);
      release_mem(&dyn_line_buf[0]);
      release_mem(&dyn_line_buf[1]);
      break;
  }

  /* initialize */
  if (enable && init) {
    col_view_scene = z64_game.scene_index;
    col_view_line = settings->bits.col_view_line;
    col_view_rd = settings->bits.col_view_rd;

    z64_col_hdr_t *col_hdr = z64_game.col_ctxt.col_hdr;

    size_t stc_poly_cap = 17 + 9 * col_hdr->n_poly;
    size_t stc_line_cap = 24 + 11 * col_hdr->n_poly;

    /* allocate static polygon display list */
    stc_poly = malloc(sizeof(*stc_poly) * stc_poly_cap);
    Gfx *stc_poly_p = stc_poly;
    Gfx *stc_poly_d = stc_poly + stc_poly_cap;
    poly_writer_t *p_poly_writer = &poly_writer;
    poly_writer_init(p_poly_writer, stc_poly_p, stc_poly_d);

    /* allocate static line display list */
    Gfx *stc_line_p = NULL;
    Gfx *stc_line_d = NULL;
    line_writer_t *p_line_writer = NULL;
    struct vector line_set;
    if (col_view_line) {
      stc_line = malloc(sizeof(*stc_line) * stc_line_cap);
      stc_line_p = stc_line;
      stc_line_d = stc_line + stc_line_cap;
      p_line_writer = &line_writer;
      line_writer_init(p_line_writer, stc_line_p, stc_line_d);
      vector_init(&line_set, sizeof(struct line));
    }

    /* allocate dynamic display lists */
    dyn_poly_buf[0] = malloc(sizeof(*dyn_poly_buf[0]) * dyn_poly_cap);
    dyn_poly_buf[1] = malloc(sizeof(*dyn_poly_buf[1]) * dyn_poly_cap);
    dyn_line_buf[0] = malloc(sizeof(*dyn_line_buf[0]) * dyn_line_cap);
    dyn_line_buf[1] = malloc(sizeof(*dyn_line_buf[1]) * dyn_line_cap);

    /* generate static display lists */
    do_poly_list(p_poly_writer, p_line_writer, &line_set,
                 col_hdr->vtx, col_hdr->poly, col_hdr->type, col_hdr->n_poly,
                 col_view_rd);

    poly_writer_finish(p_poly_writer, &stc_poly_p, &stc_poly_d);
    gSPEndDisplayList(stc_poly_p++);
    cache_writeback_data(stc_poly, sizeof(*stc_poly) * stc_poly_cap);

    if (col_view_line) {
      line_writer_finish(p_line_writer, &stc_line_p, &stc_line_d);
      gSPEndDisplayList(stc_line_p++);
      cache_writeback_data(stc_line, sizeof(*stc_line) * stc_line_cap);
      vector_destroy(&line_set);
    }

    gz.col_view_state = COLVIEW_ACTIVE;
  }

  _Bool active = gz.col_view_state == COLVIEW_ACTIVE;

  /* generate dynamic display lists */
  if (enable && (init || (active && settings->bits.col_view_upd))) {
    Gfx *dyn_poly = NULL;
    Gfx *dyn_poly_p = NULL;
    Gfx *dyn_poly_d = NULL;
    poly_writer_t *p_poly_writer = NULL;
    Gfx *dyn_line = NULL;
    Gfx *dyn_line_p = NULL;
    Gfx *dyn_line_d = NULL;
    line_writer_t *p_line_writer = NULL;

    dyn_gfx_idx = (dyn_gfx_idx + 1) % 2;

    dyn_poly = dyn_poly_buf[dyn_gfx_idx];
    dyn_poly_p = dyn_poly;
    dyn_poly_d = dyn_poly + dyn_poly_cap;
    p_poly_writer = &poly_writer;
    poly_writer_init(p_poly_writer, dyn_poly_p, dyn_poly_d);

    if (col_view_line) {
      dyn_line = dyn_line_buf[dyn_gfx_idx];
      dyn_line_p = dyn_line;
      dyn_line_d = dyn_line + dyn_line_cap;
      p_line_writer = &line_writer;
      line_writer_init(p_line_writer, dyn_line_p, dyn_line_d);
    }

    do_dyn_col(p_poly_writer, p_line_writer, col_view_rd);

    poly_writer_finish(p_poly_writer, &dyn_poly_p, &dyn_poly_d);
    gSPEndDisplayList(dyn_poly_p++);
    cache_writeback_data(dyn_poly, sizeof(*dyn_poly) * dyn_poly_cap);

    if (col_view_line) {
      line_writer_finish(p_line_writer, &dyn_line_p, &dyn_line_d);
      gSPEndDisplayList(dyn_line_p++);
      cache_writeback_data(dyn_line, sizeof(*dyn_line) * dyn_line_cap);
    }
  }

  /* draw it! */
  if (enable && active) {
    Gfx **p_gfx_p;
    Gfx **p_gfx_d;
    if (settings->bits.col_view_xlu) {
      p_gfx_p = &z64_ctxt.gfx->poly_xlu.p;
      p_gfx_d = &z64_ctxt.gfx->poly_xlu.d;
    }
    else {
      p_gfx_p = &z64_ctxt.gfx->poly_opa.p;
      p_gfx_d = &z64_ctxt.gfx->poly_opa.d;
    }

    /* polys */
    init_poly_gfx(p_gfx_p, p_gfx_d,
                  settings->bits.col_view_mode,
                  settings->bits.col_view_xlu,
                  settings->bits.col_view_shade);
    gSPSetGeometryMode((*p_gfx_p)++, G_CULL_BACK);
    gSPDisplayList((*p_gfx_p)++, stc_poly);
    gSPDisplayList((*p_gfx_p)++, dyn_poly_buf[dyn_gfx_idx]);

    /* lines */
    if (col_view_line) {
      load_l3dex2(p_gfx_p);
      init_line_gfx(p_gfx_p, p_gfx_d, settings->bits.col_view_xlu);
      gSPDisplayList((*p_gfx_p)++, stc_line);
      gSPDisplayList((*p_gfx_p)++, dyn_line_buf[dyn_gfx_idx]);
      unload_l3dex2(p_gfx_p);
      zu_set_lighting_ext(p_gfx_p, p_gfx_d);
    }
  }
}

static void do_hitbox_list(Gfx **p_gfx_p, Gfx **p_gfx_d,
                           int n_hit, z64_hit_t **hit_list,
                           uint32_t color)
{
  gDPSetPrimColor((*p_gfx_p)++, 0, 0,
                  (color >> 16) & 0xFF,
                  (color >> 8)  & 0xFF,
                  (color >> 0)  & 0xFF,
                  0xFF);

  for (int i = 0; i < n_hit; ++i) {
    z64_hit_t *hit = hit_list[i];

    switch (hit->type) {
      case Z64_HIT_SPH_LIST: {
        z64_hit_sph_list_t *hit_sph_list = (z64_hit_sph_list_t *)hit;

        for (int j = 0; j < hit_sph_list->n_ent; ++j) {
          z64_hit_sph_ent_t *ent = &hit_sph_list->ent_list[j];

          /* make zero radius spheres just appear really small */
          int radius = ent->radius;
          if (radius == 0)
            radius = 1;

          draw_ico_sphere(p_gfx_p, p_gfx_d,
                          ent->pos.x, ent->pos.y, ent->pos.z, radius);
        }

        break;
      }
      case Z64_HIT_CYL: {
        z64_hit_cyl_t *hit_cyl = (z64_hit_cyl_t *)hit;

        /* ditto for cylinders */
        int radius = hit_cyl->radius;
        if (radius == 0)
          radius = 1;

        draw_cyl(p_gfx_p, p_gfx_d,
                 hit_cyl->pos.x, hit_cyl->pos.y + hit_cyl->y_offset,
                 hit_cyl->pos.z, radius, hit_cyl->height);

        break;
      }
      case Z64_HIT_TRI_LIST: {
        z64_hit_tri_list_t *hit_tri_list = (z64_hit_tri_list_t *)hit;

        for (int j = 0; j < hit_tri_list->n_ent; ++j) {
          z64_hit_tri_ent_t *ent = &hit_tri_list->ent_list[j];

          draw_tri(p_gfx_p, p_gfx_d, &ent->v[0], &ent->v[2], &ent->v[1]);
        }

        break;
      }
      case Z64_HIT_QUAD: {
        z64_hit_quad_t *hit_quad = (z64_hit_quad_t *)hit;

        draw_quad(p_gfx_p, p_gfx_d,
                  &hit_quad->v[0], &hit_quad->v[2],
                  &hit_quad->v[3], &hit_quad->v[1]);

        break;
      }
    }
  }
}

void gz_hit_view(void)
{
  const int hit_gfx_cap = 0x800;

  static Gfx *hit_gfx_buf[2];
  static int  hit_gfx_idx = 0;

  _Bool enable = zu_in_game() && z64_game.pause_ctxt.state == 0;

  if (enable && gz.hit_view_state == HITVIEW_START) {
    hit_gfx_buf[0] = malloc(sizeof(*hit_gfx_buf[0]) * hit_gfx_cap);
    hit_gfx_buf[1] = malloc(sizeof(*hit_gfx_buf[1]) * hit_gfx_cap);

    gz.hit_view_state = HITVIEW_ACTIVE;
  }
  if (enable && gz.hit_view_state == HITVIEW_ACTIVE) {
    Gfx **p_gfx_p;
    if (settings->bits.hit_view_xlu)
      p_gfx_p = &z64_ctxt.gfx->poly_xlu.p;
    else
      p_gfx_p = &z64_ctxt.gfx->poly_opa.p;

    Gfx *hit_gfx = hit_gfx_buf[hit_gfx_idx];
    Gfx *hit_gfx_p = hit_gfx;
    Gfx *hit_gfx_d = hit_gfx + hit_gfx_cap;
    hit_gfx_idx = (hit_gfx_idx + 1) % 2;

    init_poly_gfx(&hit_gfx_p, &hit_gfx_d, SETTINGS_COLVIEW_SURFACE,
                                          settings->bits.hit_view_xlu,
                                          settings->bits.hit_view_shade);

    if (settings->bits.hit_view_oc)
      do_hitbox_list(&hit_gfx_p, &hit_gfx_d,
                     z64_game.hit_ctxt.n_oc, z64_game.hit_ctxt.oc_list, 0xFFFFFF);

    if (settings->bits.hit_view_ac)
      do_hitbox_list(&hit_gfx_p, &hit_gfx_d,
                     z64_game.hit_ctxt.n_ac, z64_game.hit_ctxt.ac_list, 0x0000FF);

    if (settings->bits.hit_view_at)
      do_hitbox_list(&hit_gfx_p, &hit_gfx_d,
                     z64_game.hit_ctxt.n_at, z64_game.hit_ctxt.at_list, 0xFF0000);

    gSPEndDisplayList(hit_gfx_p++);
    cache_writeback_data(hit_gfx, sizeof(*hit_gfx) * hit_gfx_cap);

    gSPDisplayList((*p_gfx_p)++, hit_gfx);
  }
  if (gz.hit_view_state == HITVIEW_BEGIN_STOP)
    gz.hit_view_state = HITVIEW_STOP;
  else if (gz.hit_view_state == HITVIEW_STOP) {
    release_mem(&hit_gfx_buf[0]);
    release_mem(&hit_gfx_buf[1]);

    gz.hit_view_state = HITVIEW_INACTIVE;
  }
}

static void do_path_list(Gfx **p_gfx_p, Gfx **p_gfx_d,
                           z64_path_t *path_list)
{
  const float path_marker_radius = 18.0f;

  z64_path_t *cur_path = path_list;

  if (cur_path != NULL && (settings->bits.path_view_points || settings->bits.path_view_lines)) {
    while (((cur_path->points << 4) >> 28) == Z64_SEG_SCENE && cur_path->numpoints != 0) {
      z64_xyz_t *points = (z64_xyz_t*)((z64_stab.seg[Z64_SEG_SCENE]+0x80000000) + (cur_path->points & 0x00FFFFFF));
      if (settings->bits.path_view_lines) {
        load_l3dex2(p_gfx_p);
        init_line_gfx(p_gfx_p, p_gfx_d, settings->bits.path_view_xlu);
        for (int i = 0; i < cur_path->numpoints; ++i) {
          if (i + 1 != cur_path->numpoints) {
            draw_line(p_gfx_p, p_gfx_d, &points[i], &points[i+1]);
          }
        }
        zu_set_lighting_ext(p_gfx_p, p_gfx_d);
        unload_l3dex2(p_gfx_p);
      }
      if (settings->bits.path_view_points) {
        for (int i = 0; i < cur_path->numpoints; ++i) {
          draw_ico_sphere(p_gfx_p, p_gfx_d, points[i].x, points[i].y, points[i].z, path_marker_radius);
        }
      }
      cur_path++;
    }
  }
}

void gz_path_view(void)
{
  const int path_gfx_cap = 0x800;

  static Gfx *path_gfx_buf[2];
  static int  path_gfx_idx = 0;

  _Bool enable = zu_in_game() && z64_game.pause_ctxt.state == 0;

  if (enable && gz.path_view_state == PATHVIEW_START) {
    path_gfx_buf[0] = malloc(sizeof(*path_gfx_buf[0]) * path_gfx_cap);
    path_gfx_buf[1] = malloc(sizeof(*path_gfx_buf[1]) * path_gfx_cap);

    gz.path_view_state = PATHVIEW_ACTIVE;
  }
  if (enable && gz.path_view_state == PATHVIEW_ACTIVE) {
    Gfx **p_gfx_p;
    if (settings->bits.path_view_xlu)
      p_gfx_p = &z64_ctxt.gfx->poly_xlu.p;
    else
      p_gfx_p = &z64_ctxt.gfx->poly_opa.p;

    Gfx *path_gfx = path_gfx_buf[path_gfx_idx];
    Gfx *path_gfx_p = path_gfx;
    Gfx *path_gfx_d = path_gfx + path_gfx_cap;
    path_gfx_idx = (path_gfx_idx + 1) % 2;

    do_path_list(&path_gfx_p, &path_gfx_d, z64_game.path_list);
    gSPEndDisplayList(path_gfx_p++);
    cache_writeback_data(path_gfx, sizeof(*path_gfx) * path_gfx_cap);

    gSPDisplayList((*p_gfx_p)++, path_gfx);
  }
  if (gz.path_view_state == PATHVIEW_BEGIN_STOP)
    gz.path_view_state = PATHVIEW_STOP;
  else if (gz.path_view_state == PATHVIEW_STOP) {
    release_mem(&path_gfx_buf[0]);
    release_mem(&path_gfx_buf[1]);

    gz.path_view_state = PATHVIEW_INACTIVE;
  }
}

static void do_waterbox_list(Gfx **p_gfx_p, Gfx **p_gfx_d,
                           int n_waterboxes, z64_col_water_t *waterbox_list, int cur_room,
                           uint32_t color)
{
  const float water_max_depth = -4000.0f;

  gDPSetPrimColor((*p_gfx_p)++, 0, 0,
                  (color >> 16) & 0xFF,
                  (color >> 8)  & 0xFF,
                  (color >> 0)  & 0xFF,
                  0xFF);

  for (z64_col_water_t *cur_water = waterbox_list; cur_water < waterbox_list + n_waterboxes; ++cur_water) {
    if (cur_water->flags.group == 0x3F || cur_water->flags.group == cur_room) {
      z64_xyzf_t points[] = {
        { cur_water->pos.x, cur_water->pos.y, cur_water->pos.z },
        { cur_water->pos.x + cur_water->width, cur_water->pos.y, cur_water->pos.z },
        { cur_water->pos.x + cur_water->width, cur_water->pos.y, cur_water->pos.z + cur_water->depth },
        { cur_water->pos.x, cur_water->pos.y, cur_water->pos.z + cur_water->depth },
        { cur_water->pos.x, water_max_depth, cur_water->pos.z },
        { cur_water->pos.x + cur_water->width, water_max_depth, cur_water->pos.z },
        { cur_water->pos.x + cur_water->width, water_max_depth, cur_water->pos.z + cur_water->depth },
        { cur_water->pos.x, water_max_depth, cur_water->pos.z + cur_water->depth },
      };
      draw_quad(p_gfx_p, p_gfx_d, &points[0], &points[1], &points[2], &points[3]);
      draw_quad(p_gfx_p, p_gfx_d, &points[0], &points[1], &points[5], &points[4]);
      draw_quad(p_gfx_p, p_gfx_d, &points[1], &points[2], &points[6], &points[5]);
      draw_quad(p_gfx_p, p_gfx_d, &points[2], &points[3], &points[7], &points[6]);
      draw_quad(p_gfx_p, p_gfx_d, &points[3], &points[0], &points[4], &points[7]);
    }
  }
}

void gz_water_view(void)
{
  const int water_gfx_cap = 0x800;

  static Gfx *water_gfx_buf[2];
  static int  water_gfx_idx = 0;

  _Bool enable = zu_in_game() && z64_game.pause_ctxt.state == 0;

  if (enable && gz.water_view_state == WATERVIEW_START) {
    water_gfx_buf[0] = malloc(sizeof(*water_gfx_buf[0]) * water_gfx_cap);
    water_gfx_buf[1] = malloc(sizeof(*water_gfx_buf[1]) * water_gfx_cap);

    gz.water_view_state = WATERVIEW_ACTIVE;
  }
  if (enable && gz.water_view_state == WATERVIEW_ACTIVE && z64_game.col_ctxt.col_hdr->n_water != 0) {
    Gfx **p_gfx_p;
    if (settings->bits.water_view_xlu)
      p_gfx_p = &z64_ctxt.gfx->poly_xlu.p;
    else
      p_gfx_p = &z64_ctxt.gfx->poly_opa.p;

    Gfx *water_gfx = water_gfx_buf[water_gfx_idx];
    Gfx *water_gfx_p = water_gfx;
    Gfx *water_gfx_d = water_gfx + water_gfx_cap;
    water_gfx_idx = (water_gfx_idx + 1) % 2;

    init_poly_gfx(&water_gfx_p, &water_gfx_d, SETTINGS_COLVIEW_SURFACE,
                                          settings->bits.water_view_xlu,
                                          settings->bits.water_view_shade);
    do_waterbox_list(&water_gfx_p, &water_gfx_d,
                    z64_game.col_ctxt.col_hdr->n_water, z64_game.col_ctxt.col_hdr->water,
                    z64_game.room_ctxt.rooms[0].index, 0x57ACF3);
    for (int i = 0; i < 50; ++i) {
      if (z64_game.col_ctxt.dyn_flags[i].active) {
        z64_col_hdr_t *dyn_col_hdr = z64_game.col_ctxt.dyn_col[i].col_hdr;

        do_waterbox_list(&water_gfx_p, &water_gfx_d,
                    dyn_col_hdr->n_water, dyn_col_hdr->water,
                    z64_game.room_ctxt.rooms[0].index, 0x57ACF3);
      }
    }

    gSPEndDisplayList(water_gfx_p++);
    cache_writeback_data(water_gfx, sizeof(*water_gfx) * water_gfx_cap);

    gSPDisplayList((*p_gfx_p)++, water_gfx);
  }
  if (gz.water_view_state == WATERVIEW_BEGIN_STOP)
    gz.water_view_state = WATERVIEW_STOP;
  else if (gz.water_view_state == WATERVIEW_STOP) {
    release_mem(&water_gfx_buf[0]);
    release_mem(&water_gfx_buf[1]);

    gz.water_view_state = WATERVIEW_INACTIVE;
  }
}
