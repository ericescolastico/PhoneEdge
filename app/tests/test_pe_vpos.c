#include "common.h"

#include <assert.h>
#include <math.h>

#include "edge/pe_vpos.h"

static void test_init_center(void) {
    struct pe_vpos v;
    pe_vpos_init(&v, 1080, 2340);
    assert(fabsf(v.x - 539.5f) < 0.01f);
    assert(fabsf(v.y - 1169.5f) < 0.01f);
}

static void test_move_inside(void) {
    struct pe_vpos v;
    pe_vpos_init(&v, 100, 200);
    pe_vpos_set(&v, 10, 20);
    float cx = 1, cy = 1;
    pe_vpos_move(&v, 5, -3, &cx, &cy);
    assert(v.x == 15 && v.y == 17);
    assert(cx == 0 && cy == 0);
}

static void test_clamp_all_edges(void) {
    struct pe_vpos v;
    pe_vpos_init(&v, 100, 200);
    float cx, cy;

    pe_vpos_set(&v, 10, 10);
    pe_vpos_move(&v, -25, -4, &cx, &cy);
    assert(v.x == 0 && v.y == 6);
    assert(cx == -15 && cy == 0);

    pe_vpos_set(&v, 90, 190);
    pe_vpos_move(&v, 20, 30, &cx, &cy);
    assert(v.x == 99 && v.y == 199);
    assert(cx == 11 && cy == 21);

    pe_vpos_move(&v, 0, -1000, &cx, &cy);
    assert(v.y == 0 && cy == -801);
}

static void test_set_clamps(void) {
    struct pe_vpos v;
    pe_vpos_init(&v, 100, 200);
    pe_vpos_set(&v, -5, 500);
    assert(v.x == 0 && v.y == 199);
}

static void test_null_cut_pointers(void) {
    struct pe_vpos v;
    pe_vpos_init(&v, 100, 200);
    pe_vpos_move(&v, 1000, 1000, NULL, NULL);
    assert(v.x == 99 && v.y == 199);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_init_center();
    test_move_inside();
    test_clamp_all_edges();
    test_set_clamps();
    test_null_cut_pointers();
    return 0;
}
