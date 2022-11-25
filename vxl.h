#ifndef VXL_H

#include <stdint.h>

#include "common.h"

#define CHUNK_LENGTH_LOG2 (3)
#define CHUNK_LENGTH (1 << CHUNK_LENGTH_LOG2)
#define CHUNK_LENGTH_MASK (CHUNK_LENGTH - 1)

struct vxl {
	int dim_x;
	int dim_y;
	int dim_z;
	int chunk_dim_x;
	int chunk_dim_y;
	int chunk_dim_z;
	int cdxy;

	u8* data;
	u8* shade;

	int shade_queue_len, shade_queue_cap;
	union ivec3* shade_queue;

	int render_queue_len, render_queue_cap;
	union ivec3* render_queue;

	int bitmap_width;
	int bitmap_height;
	u32* bitmap;

	int rotation;
	int rotation_vx;
	int rotation_vy;

	int full_update;
};

static inline int vxl_chunk_idx(struct vxl* vxl, int cx, int cy, int cz)
{
	return (cx) + (cy * vxl->chunk_dim_x) + (cz * vxl->cdxy);
}

static inline int vxl_local_idx(struct vxl* vxl, int x, int y, int z)
{
	return (x) + (y << CHUNK_LENGTH_LOG2) + (z << (2*CHUNK_LENGTH_LOG2));
}

static inline int vxl_idx(struct vxl* vxl, int x, int y, int z)
{
	int chunk_x = x >> CHUNK_LENGTH_LOG2;
	int chunk_y = y >> CHUNK_LENGTH_LOG2;
	int chunk_z = z >> CHUNK_LENGTH_LOG2;
	int chunk_index = vxl_chunk_idx(vxl, chunk_x, chunk_y, chunk_z);

	int local_x = x & CHUNK_LENGTH_MASK;
	int local_y = y & CHUNK_LENGTH_MASK;
	int local_z = z & CHUNK_LENGTH_MASK;
	int local_index = vxl_local_idx(vxl, local_x, local_y, local_z);

	return local_index + (chunk_index << (3*CHUNK_LENGTH_LOG2));
}

static inline int vxl_inside(struct vxl* vxl, int x, int y, int z)
{
	return x >= 0 && y >= 0 && z >= 0 && x < vxl->dim_x && y < vxl->dim_y && z < vxl->dim_z;
}

static inline int vxl_chkidx(struct vxl* vxl, int x, int y, int z)
{
	if (vxl_inside(vxl, x, y, z)) {
		return vxl_idx(vxl, x, y, z);
	} else {
		return -1;
	}
}

void vxl_flush(struct vxl* vxl);
int vxl_put(struct vxl* vxl, int x, int y, int z, uint8_t v);

void vxl_init(struct vxl* vxl, int dim_x, int dim_y, int dim_z);

// sets "full update mode" which lasts until the next vxl_flush() call, which
// will shade/render everything, not only voxels affected since last flush
// (which may happen implicitly/automatically when using vxl_put()). NOTE that
// direct manipulation of the vxl->data array (e.g. with the help of vxl_idx())
// is OK when in "full update" mode, whereas vxl_put() is recommended othewise.
static inline void vxl_set_full_update(struct vxl* vxl)
{
	if (vxl->full_update) return;
	vxl_flush(vxl);
	vxl->full_update = 1;
}

static inline void vxl_set_rotation(struct vxl* vxl, int rotation)
{
	rotation = rotation & 3;

	// calculate view x/y from rotation
	{
		int vx = -1;
		int vy = -1;
		for (int i = 0; i < rotation; i++) {
			int tmp = vx;
			vx = vy;
			vy = -tmp;
		}
		vxl->rotation_vx = vx;
		vxl->rotation_vy = vy;
	}

	if (rotation != vxl->rotation) {
		vxl->rotation = rotation;
		vxl_set_full_update(vxl);
	}
}

#define VXL_H
#endif
