#include "walk.h"
#include <string.h>

/* Высота пола клетки с учётом рампы: для CELL_STAIR высота интерполируется между
 * двумя противоположными соседями по оси с большей разницей высот. */
/* Клетка вращающегося сегмента проходима только в нужном состоянии сегмента. */
static int seg_open(const level_t *l, const level_cell_t *c) {
    if (!(c->flags & CELL_SEG)) return 1;
    int id = CELL_SEG_ID(c->segment);
    if (id <= 0 || id >= LEVEL_MAX_SEGMENTS) return 1;
    return l->seg_states[id] == CELL_SEG_STATE(c->segment);
}

static float cell_floor(const level_t *l, int cx, int cz, float local_x, float local_z) {
    const level_cell_t *c = level_cell(l, cx, cz);
    if (!c || !(c->flags & CELL_EXISTS) || !(c->flags & CELL_WALK)) return WALK_NO_FLOOR;
    if (!seg_open(l, c)) return WALK_NO_FLOOR;
    if (level_cell_blocked(l, cx, cz)) return WALK_NO_FLOOR; /* там стоит блок */
    /* Плавучая клетка — плот: он всплывает и опускается вместе с водой, поэтому пол
     * у неё не из карты, а из уровня воды. Подняться на плот или сойти с него можно,
     * только пока перепад укладывается в шаг — в этом и состоит головоломка. */
    if (c->flags & CELL_FLOAT) return (float)l->water_steps * l->step_y;

    float base = (float)c->height * l->step_y;
    if (!(c->flags & CELL_STAIR)) return base;

    float west = level_cell_top(l, cx - 1, cz), east = level_cell_top(l, cx + 1, cz);
    float north = level_cell_top(l, cx, cz - 1), south = level_cell_top(l, cx, cz + 1);
    float dx = (west > -1.0e8f && east > -1.0e8f) ? (east - west) : 0.0f;
    float dz = (north > -1.0e8f && south > -1.0e8f) ? (south - north) : 0.0f;
    float adx = dx < 0.0f ? -dx : dx;
    float adz = dz < 0.0f ? -dz : dz;
    if (adx < 0.001f && adz < 0.001f) return base;

    if (adx >= adz) return west + dx * local_x;
    return north + dz * local_z;
}

float walk_floor_at(const level_t *l, float x, float z) {
    int cx = 0, cz = 0;
    if (!level_cell_at(l, x, z, &cx, &cz)) return WALK_NO_FLOOR;
    float ox = -(float)l->cells_x * l->cell_size * 0.5f;
    float oz = -(float)l->cells_z * l->cell_size * 0.5f;
    float local_x = (x - ox) / l->cell_size - (float)cx;
    float local_z = (z - oz) / l->cell_size - (float)cz;
    if (local_x < 0.0f) local_x = 0.0f;
    if (local_x > 1.0f) local_x = 1.0f;
    if (local_z < 0.0f) local_z = 0.0f;
    if (local_z > 1.0f) local_z = 1.0f;
    return cell_floor(l, cx, cz, local_x, local_z);
}

int walk_is_walkable(const level_t *l, int cx, int cz) {
    const level_cell_t *c = level_cell(l, cx, cz);
    if (!c || !(c->flags & CELL_EXISTS) || !(c->flags & CELL_WALK)) return 0;
    return (seg_open(l, c) && !level_cell_blocked(l, cx, cz)) ? 1 : 0;
}

/* Круг радиуса r вокруг (x, z) стоит на полу, если во всех пробных точках есть пол
 * и перепад с текущей высоты не больше step_max. */
static int circle_ok(const level_t *l, float x, float z, float radius, float from_y, float step_max,
                     float *out_y) {
    static const float dirs[8][2] = {
        { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f },
        { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f }
    };
    float center = walk_floor_at(l, x, z);
    if (center <= WALK_NO_FLOOR * 0.5f) return 0;
    float diff = center - from_y;
    if (diff > step_max || diff < -step_max) return 0;
    float highest = center;
    for (int i = 0; i < 8; i++) {
        float px = x + dirs[i][0] * radius;
        float pz = z + dirs[i][1] * radius;
        float h = walk_floor_at(l, px, pz);
        if (h <= WALK_NO_FLOOR * 0.5f) return 0;
        float d = h - from_y;
        if (d > step_max || d < -step_max) return 0;
        if (h > highest) highest = h;
    }
    if (out_y) *out_y = highest;
    return 1;
}

int walk_stand_ok(const level_t *l, float x, float z, float radius, float step_max, float *out_y) {
    if (!l) return 0;
    float floor_y = walk_floor_at(l, x, z);
    if (floor_y <= WALK_NO_FLOOR * 0.5f) return 0;
    return circle_ok(l, x, z, radius, floor_y, step_max, out_y);
}

int walk_move(const level_t *l, walk_pos_t *pos, float dx, float dz, float radius, float step_max) {
    if (!l || !pos) return 0;
    float start_x = pos->x, start_z = pos->z;
    float y = pos->y, new_y = pos->y;

    if (dx != 0.0f) {
        float nx = pos->x + dx;
        if (circle_ok(l, nx, pos->z, radius, y, step_max, &new_y)) {
            pos->x = nx;
            pos->y = new_y;
        }
    }
    if (dz != 0.0f) {
        float nz = pos->z + dz;
        if (circle_ok(l, pos->x, nz, radius, pos->y, step_max, &new_y)) {
            pos->z = nz;
            pos->y = new_y;
        }
    }
    return (pos->x != start_x || pos->z != start_z) ? 1 : 0;
}

void walk_spawn(const level_t *l, walk_pos_t *pos) {
    if (!l || !pos) return;
    pos->x = l->spawn_x;
    pos->z = l->spawn_z;
    float y = walk_floor_at(l, pos->x, pos->z);
    pos->y = (y <= WALK_NO_FLOOR * 0.5f) ? 0.0f : y;
}
