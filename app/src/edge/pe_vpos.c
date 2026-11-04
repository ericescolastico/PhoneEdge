#include "pe_vpos.h"

static float
clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void
pe_vpos_init(struct pe_vpos *v, int width, int height) {
    v->width = width;
    v->height = height;
    v->x = (float) (width - 1) / 2;
    v->y = (float) (height - 1) / 2;
}

void
pe_vpos_set(struct pe_vpos *v, float x, float y) {
    v->x = clampf(x, 0, (float) (v->width - 1));
    v->y = clampf(y, 0, (float) (v->height - 1));
}

void
pe_vpos_move(struct pe_vpos *v, float dx, float dy, float *cut_x,
             float *cut_y) {
    float nx = v->x + dx;
    float ny = v->y + dy;
    float maxx = (float) (v->width - 1);
    float maxy = (float) (v->height - 1);

    float cx = 0;
    float cy = 0;
    if (nx < 0) {
        cx = nx;
    } else if (nx > maxx) {
        cx = nx - maxx;
    }
    if (ny < 0) {
        cy = ny;
    } else if (ny > maxy) {
        cy = ny - maxy;
    }

    v->x = clampf(nx, 0, maxx);
    v->y = clampf(ny, 0, maxy);

    if (cut_x) {
        *cut_x = cx;
    }
    if (cut_y) {
        *cut_y = cy;
    }
}
