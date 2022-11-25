#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "vxl.h"
#include "common.h"

/*

VOXEL RENDERING

A 4×4×1 shape...

       1234
       5678
       9abc
       defg

...is rendered like this:

        11
      551122
    9955662233
  dd99aa66773344
  ddeeaabb778844
    eeffbbcc88
      ffggcc
        gg

So the X/Y plane is "the ground", while the Z-axis represents height.
Z-positioning works so that a voxel on top of "g" shadows the "b"-voxel
completely. More generally, a (x+k,y+k,z+k) voxel shadows the (x,y,z) voxel
completely, for integer k>0.

So the projection from 3d voxel position (vx,vy,vz) to 2d render position
(rx,ry) is:

   rx = (vx-vy)*2
   ry = (vx+vy) - 2*vz

Note that the stage can be rotated in 90° steps around the Z-axis by shuffling
X/Y stuff around a bit in the above math.

*/


static inline void vxl_bounding_rect(int* w, int* h, int dx, int dy, int dz)
{
	*w = 2*(dx+dy-1);
	*h = dx + dy + (dz-1)*2;
}

static inline int diagonal_count(int dx, int dy, int dz)
{
	return dx*dy + (dz-1)*(dx+dy-1);
}

static inline int count_chunk_diagonals()
{
	return diagonal_count(CHUNK_LENGTH, CHUNK_LENGTH, CHUNK_LENGTH);
}

static union ivec3* mk_ivec3_queue(int* pcap, int cap)
{
	*pcap = cap;
	union ivec3* r = malloc(cap * sizeof(*r));
	assert(r != NULL);
	return r;
}

void vxl_init(struct vxl* vxl, int dim_x, int dim_y, int dim_z)
{
	memset(vxl, 0, sizeof* vxl);

	// XXX do I want this potceil stuff? it effectively "expands" the
	// arena, opening it up for celluar automata and such?
	dim_x = potceil(dim_x, CHUNK_LENGTH_LOG2);
	dim_y = potceil(dim_y, CHUNK_LENGTH_LOG2);
	dim_z = potceil(dim_z, CHUNK_LENGTH_LOG2);

	vxl->dim_x = dim_x;
	vxl->dim_y = dim_y;
	vxl->dim_z = dim_z;

	int chunk_dim_x = vxl->chunk_dim_x = dim_x >> CHUNK_LENGTH_LOG2;
	int chunk_dim_y = vxl->chunk_dim_y = dim_y >> CHUNK_LENGTH_LOG2;
	int chunk_dim_z = vxl->chunk_dim_z = dim_z >> CHUNK_LENGTH_LOG2;
	vxl->cdxy = chunk_dim_x * chunk_dim_y;

	int n_voxels = dim_x * dim_y * dim_z;

	assert((vxl->data = calloc(n_voxels, sizeof *vxl->data)) != NULL);
	assert((vxl->shade = calloc(n_voxels, sizeof *vxl->shade)) != NULL);

	int n_chunks = chunk_dim_x * chunk_dim_y * chunk_dim_z;

	vxl_bounding_rect(&vxl->bitmap_width, &vxl->bitmap_height, dim_x, dim_y, dim_z);
	assert((vxl->bitmap = calloc(vxl->bitmap_width * vxl->bitmap_height, sizeof *vxl->bitmap)) != NULL);

	#ifdef DEBUG
	printf("vxl n_voxels: %d\n", n_voxels);
	printf("vxl n_chunks: %d\n", n_chunks);
	printf("vxl bitmap: %d × %d\n", vxl->bitmap_width, vxl->bitmap_height);
	#endif

	{
		const int base_cap = 1<<14;
		vxl->shade_queue  = mk_ivec3_queue(&vxl->shade_queue_cap,  4*base_cap);
		vxl->render_queue = mk_ivec3_queue(&vxl->render_queue_cap, base_cap);
	}

	vxl_set_rotation(vxl, 0);
}

#define XA_VXYZ(vx,vy,vz) XA(((vx) == 1 || (vx) == -1) && ((vy) == 1 || (vy) == -1) && ((vz) == 1 || (vz) == -1))

// diagonal distance to edge of AABB with dimensions [dx,dy,dz] along vector
// [vx,vy,vz] from position [x,y,z]. (not eucledean distance; 3 steps along
// [vx,vy,vz] means 3 is returned)
static inline int diagonal_dist(int vx, int vy, int vz, int dx, int dy, int dz, int x, int y, int z)
{
	XA_VXYZ(vx, vy, vz);
	int nx = (vx<0) ? (x) : (dx-x-1);
	int ny = (vy<0) ? (y) : (dy-y-1);
	int nz = (vz<0) ? (z) : (dz-z-1);
	return MIN(nx, MIN(ny, nz));
}

static inline void as_diagonal(int vx, int vy, int vz, int dx, int dy, int dz, int* x, int* y, int* z)
{
	XA_VXYZ(vx, vy, vz);
	int n = diagonal_dist(vx, vy, vz, dx, dy, dz, *x, *y, *z);
	*x += vx*n;
	*y += vy*n;
	*z += vz*n;
}

static int ivec3cmp(const void* va, const void* vb)
{
	// XXX might be better to do a "proper compare"? because memcmp() is
	// "big-endian" by necessity, so the ordering is weird on little-endian
	// archs (for correctness, only consistent ordering is required,
	// including that memcmp() must return zero for equal ivec3's)
	return memcmp(va, vb, sizeof(union ivec3));
}

#define SHADE_X  (1)
#define SHADE_Y  (2)
#define SHADE_Z  (3)
#define SHADE_XY (4)

static inline void update_shade(struct vxl* vxl, int x, int y, int z)
{
	int vx = vxl->rotation_vx;
	int vy = vxl->rotation_vy;
	const int vz = -1;

	XA_VXYZ(vx,vy,vz);

	int nx = vxl->data[vxl_idx(vxl, x-vx, y,    z   )] == 0;
	int ny = vxl->data[vxl_idx(vxl, x,    y-vy, z   )] == 0;
	int nz = vxl->data[vxl_idx(vxl, x,    y,    z-vz)] == 0;

	u8 set_shade;
	if (nz) {
		set_shade = SHADE_Z;
	} else if (nx && !ny) {
		set_shade = SHADE_X;
	} else if (ny && !nx) {
		set_shade = SHADE_Y;
	} else if (nx && ny) {
		set_shade = SHADE_XY;
	} else {
		set_shade = SHADE_X; // XXX good default?
	}

	vxl->shade[vxl_idx(vxl, x, y, z)] = set_shade;
}

static inline void get_voxel_rgba(u32* rgba0, u32* rgba1, u8 voxel, u8 shade)
{
	if (shade == SHADE_X) {
		*rgba0 = 0xff555555;
		*rgba1 = 0xff555555;
	} else if (shade == SHADE_Y) {
		*rgba0 = 0xff777777;
		*rgba1 = 0xff777777;
	} else if (shade == SHADE_Z) {
		*rgba0 = 0xffaaaaaa;
		*rgba1 = 0xffaaaaaa;
	} else if (shade == SHADE_XY) {
		*rgba0 = 0xff777777;
		*rgba1 = 0xff555555;
	}
}


static inline void render_diagonal(struct vxl* vxl, int x, int y, int z)
{
	const int vx = vxl->rotation_vx;
	const int vy = vxl->rotation_vy;
	const int vz = -1;

	const int dx = vxl->dim_x;
	const int dy = vxl->dim_y;
	const int dz = vxl->dim_z;

	u32 rgba0 = 0;
	u32 rgba1 = 0;

	int dist = diagonal_dist(
		vx, vy, vz,
		dx, dy, dz,
		x, y, z);

	for (int i = 0; i <= dist; i++) {
		int idx = vxl_idx(vxl, x, y, z);
		u8 v = vxl->data[idx];
		if (v > 0) {
			u8 s = vxl->shade[idx];
			get_voxel_rgba(&rgba0, &rgba1, v, s);
			break;
		}

		x += vx;
		y += vy;
		z += vz;
	}

	// projection

	int xq = x;
	int yq = y;
	int zq = z;

	int dxq = dx;
	int dyq = dy;
	int dzq = dz;

	for (int i = 0; i < vxl->rotation; i++) {
		// XXX TODO can't I transform the *q variables to do the right
		// thing like this?
	}

	const int sx = 2*(dxq-1+xq-yq);
	const int sy = (xq+yq) + 2*(dzq-1-zq);

	XA(sx >= 0);
	XA(sy >= 0);
	XA(sx < vxl->bitmap_width);
	XA(sy < vxl->bitmap_height);

	// draw "fat pixel"
	const int w = vxl->bitmap_width;
	u32* pixel = &vxl->bitmap[sx + sy*w];
	pixel[0]   = rgba0;
	pixel[1]   = rgba1;
	pixel[w]   = rgba0;
	pixel[w+1] = rgba1;
}

static void clear_bitmap(struct vxl* vxl)
{
	memset(vxl->bitmap, 0, vxl->bitmap_width * vxl->bitmap_height * sizeof(*vxl->bitmap));
}

void vxl_flush(struct vxl* vxl)
{
	if (vxl->full_update) {
		clear_bitmap(vxl);

		const int dx = vxl->dim_x;
		const int dy = vxl->dim_y;
		const int dz = vxl->dim_z;

		const int vx = vxl->rotation_vx;
		const int vy = vxl->rotation_vy;

		// full per-voxel shade update
		{

			const int x0 = vx > 0 ? 1    : 0;
			const int x1 = vx > 0 ? (dx) : (dx-1);
			const int y0 = vy > 0 ? 1    : 0;
			const int y1 = vy > 0 ? (dy) : (dy-1);
			const int z0 = 0;
			const int z1 = dz-1;

			for (int z = z0; z < z1; z++) {
				for (int y = y0; y < y1; y++) {
					for (int x = x0; x < x1; x++) {
						update_shade(vxl, x, y, z);
					}
				}
			}
		}

		// render all diagonals
		{
			// top
			for (int v = 0; v < dy; v++) {
				for (int u = 0; u < dx; u++) {
					render_diagonal(vxl, u, v, dz-1);
				}
			}

			// rotation 0 has X+ and Y+ facing the camera, i.e. "d"
			// through "g" through "4" are visible.
			//   1234-
			//   5678-
			//   9abc-
			//   defg-
			//   ||||
			//
			// rotation 1 then has Y- and X+ facing the camera
			//   d951-
			//   ea62-
			//   fb73-
			//   gc84-
			//   ||||

			// sides
			int xfront = vx < 0 ? dx-1 : 0;
			int yfront = vy < 0 ? dy-1 : 0;
			for (int v = 0; v < (dz-1); v++) {
				// XXX FIXME there's a bit of overdraw; a
				// "shared column" between the two loops
				for (int u = 0; u < dx; u++) {
					render_diagonal(vxl, u, yfront, v);
				}

				for (int u = 0; u < dy; u++) {
					render_diagonal(vxl, xfront, u, v);
				}
			}
		}

		vxl->full_update = 0;

		#ifdef DEBUG
		printf("vxl_flush: FULL\n");
		#endif
	} else {
		int n_shaded = 0;
		int shade_queue_len = vxl->shade_queue_len;
		{
			qsort(vxl->shade_queue, shade_queue_len, sizeof *vxl->shade_queue, ivec3cmp);
			union ivec3* p = vxl->shade_queue;
			for (int i = 0; i < shade_queue_len; i++, p++) {
				if (i > 0 && ivec3cmp(p, p-1) == 0) continue; // skip dupes
				update_shade(vxl, p->x, p->y, p->z);
				n_shaded++;
			}
			vxl->shade_queue_len = 0;
		}

		int n_rendered = 0;
		int render_queue_len = vxl->render_queue_len;
		{
			qsort(vxl->render_queue, render_queue_len, sizeof *vxl->render_queue, ivec3cmp);
			union ivec3* p = vxl->render_queue;
			for (int i = 0; i < render_queue_len; i++, p++) {
				if (i > 0 && ivec3cmp(p, p-1) == 0) continue; // skip dupes
				render_diagonal(vxl, p->x, p->y, p->z);
				n_rendered++;
			}
			vxl->render_queue_len = 0;
		}

		#ifdef DEBUG
		printf("vxl_flush: shaded %d/%d; rendered %d/%d\n", n_shaded, shade_queue_len, n_rendered, render_queue_len);
		#endif
	}

	assert(vxl->full_update == 0);
	assert(vxl->shade_queue_len == 0);
	assert(vxl->render_queue_len == 0);
}

int vxl_put(struct vxl* vxl, int x, int y, int z, u8 v)
{
	if (!vxl_inside(vxl, x, y, z)) return 0;
	int idx = vxl_idx(vxl, x, y, z);

	u8 p = vxl->data[idx];
	vxl->data[idx] = v;

	if (vxl->full_update || p == v) {
		// if in "full update" mode, or if the put is a no-op, bail
		// early because the rest deals with shade/render queues
		return 0;
	}

	//const int shade_max_req = 4;
	const int shade_max_req = 3*3*3; // XXX
	const int render_req = 1;

	int must_flush =
		   (vxl->shade_queue_len  + shade_max_req > vxl->shade_queue_cap)
		|| (vxl->render_queue_len + render_req    > vxl->render_queue_cap);

	int ret = 0;
	if (must_flush) {
		ret = 1;
		vxl_flush(vxl);

		#ifdef DEBUG
		// asserting that flushing "did its thing" so that we have the required
		// capacity
		must_flush =
			   (vxl->shade_queue_len  + shade_max_req > vxl->shade_queue_cap)
			|| (vxl->render_queue_len + render_req    > vxl->render_queue_cap);
		assert(!must_flush);
		#endif
	}

	int do_update_shade = (p == 0) != (v == 0);

	union ivec3 ref = (union ivec3){ .x=x, .y=y, .z=z };

	if (do_update_shade) {
		#if 0
		// push self
		{
			union ivec3* s = &vxl->shade_queue[vxl->shade_queue_len++];
			*s = ref;
		}

		// push X-side
		int vx = vxl->rotation_vx;
		if ((vx < 0 && x > 0) || (vx > 0 && x < (vxl->dim_x-1))) {
			union ivec3* s = &vxl->shade_queue[vxl->shade_queue_len++];
			*s = ref;
			s->x = x + vx;
		}

		// push Y-side
		int vy = vxl->rotation_vy;
		if ((vy < 0 && y > 0) || (vy > 0 && y < (vxl->dim_y-1))) {
			union ivec3* s = &vxl->shade_queue[vxl->shade_queue_len++];
			*s = ref;
			s->y = y + vy;
		}

		// push Z-side (top)
		if (z > 0) {
			union ivec3* s = &vxl->shade_queue[vxl->shade_queue_len++];
			*s = ref;
			s->z = z - 1;
		}
		#endif

		for (int az = -1; az <= 1; az++) {
			for (int ay = -1; ay <= 1; ay++) {
				for (int ax = -1; ax <= 1; ax++) {
					int x1 = x+ax;
					int y1 = y+ay;
					int z1 = z+az;
					if (x1 < 0 && x1 >= vxl->dim_x) continue;
					if (y1 < 0 && y1 >= vxl->dim_y) continue;
					if (z1 < 0 && z1 >= vxl->dim_z) continue;

					union ivec3* s = &vxl->shade_queue[vxl->shade_queue_len++];
					s->x = x1;
					s->y = y1;
					s->z = z1;
				}
			}
		}
	}

	{
		union ivec3* r = &vxl->render_queue[vxl->render_queue_len++];
		*r = ref;
		as_diagonal(
			-vxl->rotation_vx, -vxl->rotation_vy, 1,
			vxl->dim_x, vxl->dim_y, vxl->dim_z,
			&r->x, &r->y, &r->z);
	}

	return ret;
}
