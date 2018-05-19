#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "math-prefix-qry.h"

struct math_prefix_qry
pq_allocate(uint32_t q_maxid)
{
	struct math_prefix_qry pq;
	uint32_t i;

	pq.n = q_maxid + 1;
	pq.n_dirty = 0;

	pq.dirty = calloc(pq.n * MAX_NODE_IDS, sizeof(struct math_prefix_loc));

	pq.cell = calloc(pq.n, sizeof(struct math_prefix_cell *));
	for (i = 0; i < pq.n; i++)
		pq.cell[i] = calloc(MAX_NODE_IDS, sizeof(struct math_prefix_cell));

 	return pq;
}

void pq_free(struct math_prefix_qry pq)
{
	for (uint32_t i = 0; i < pq.n; i++)
		free(pq.cell[i]);
	free(pq.cell);

	free(pq.dirty);
}

void pq_reset(struct math_prefix_qry *pq)
{
	for (uint32_t i = 0; i < pq->n_dirty; i++) {
		struct math_prefix_loc loc = pq->dirty[i];
		memset(&pq->cell[loc.qr][loc.dr], 0, sizeof(struct math_prefix_cell));
	}

	pq->n_dirty = 0;
}

#define C_GRAY    "\033[1m\033[30m"
#define C_RST     "\033[0m"

void pq_print(struct math_prefix_qry pq, uint32_t d_maxid)
{
	uint32_t i, j, k;
	//printf("n_dirty = %u\n", pq.n_dirty);

	printf("%3s | ", "qr");
	for (j = 0; j < d_maxid; j++) {
		printf("[%2u] ", j);
	}
	printf("\n");

	for (i = 0; i < pq.n; i++) {
		printf("%3u | ", i);
		for (j = 0; j < d_maxid; j++) {
			struct math_prefix_loc loc = {i, j};
			for (k = 0; k < pq.n_dirty; k++) {
				if (0 == memcmp(&loc, pq.dirty + k,
				                sizeof(struct math_prefix_loc)))
					break;
			}

			if (k == pq.n_dirty)
				printf(C_GRAY);
			printf(" %2u  ", pq.cell[i][j].cnt);
			printf(C_RST);
		}
		printf("\n");
	}
}

uint64_t pq_hit(struct math_prefix_qry *pq,
                uint32_t qr, uint32_t ql,
                uint32_t dr, uint32_t dl)
{
	struct math_prefix_cell *cell;
	uint64_t ql_bit, dl_bit;

	if (dr >= MAX_NODE_IDS)
		return dr;

	cell = pq->cell[qr] + dr;
	if (cell->cnt == 0) {
		pq->dirty[pq->n_dirty].qr = qr;
		pq->dirty[pq->n_dirty].dr = dr;
		pq->n_dirty++;
	}

	ql_bit = 1ULL << (ql - 1);
	dl_bit = 1ULL << (dl - 1);

	if (ql_bit & cell->qmask) {
		cell->dmiss ++;
		return cell->qmask;
	} else if (dl_bit & cell->dmask) {
		cell->qmiss ++;
		return cell->dmask;
	} else {
		cell->cnt += 1;
		cell->qmask |= ql_bit;
		cell->dmask |= dl_bit;
		return 0;
	}
}

void counting_sort(struct math_prefix_qry *pq)
{
	uint32_t i, count[MAX_CELL_CNT] = {0};
	struct math_prefix_loc dirty[pq->n_dirty];

	for (i = 0; i < pq->n_dirty; i++) {
		struct math_prefix_loc   loc = pq->dirty[i];
		struct math_prefix_cell *cell = &pq->cell[loc.qr][loc.dr];
		count[cell->cnt] ++;
	}

	for (i = 1; i < MAX_CELL_CNT; ++i)
		count[i] += count[i - 1];
	
	for (i = 0; i < pq->n_dirty; i++) {
		struct math_prefix_loc   loc = pq->dirty[i];
		struct math_prefix_cell *cell = &pq->cell[loc.qr][loc.dr];
		dirty[count[cell->cnt] - 1] = pq->dirty[i];
		count[cell->cnt] -= 1;
	}

	memcpy(pq->dirty, dirty, sizeof(struct math_prefix_loc) * pq->n_dirty);
}

void print_dirty_array(struct math_prefix_qry *pq)
{
	uint32_t i;
	for (i = 0; i < pq->n_dirty; i++) {
		struct math_prefix_loc   loc = pq->dirty[i];
		struct math_prefix_cell *cell = &pq->cell[loc.qr][loc.dr];
		printf("[%u]", cell->cnt);
		// printf("%u == %u == %u \n",
		// 	__builtin_popcountll(cell->qmask),
		// 	__builtin_popcountll(cell->dmask),
		// 	cell->cnt
		// );
	}
	printf("\n");
}

uint32_t pq_align(struct math_prefix_qry *pq, uint32_t *topk,
                  struct math_prefix_loc *rmap, uint32_t k)
{
	uint32_t i, j = 0;
	uint64_t exclude_qmask = 0;
	uint64_t exclude_dmask = 0;
	struct math_prefix_loc loc;
	struct math_prefix_cell *cell;
	uint32_t n_joint_nodes = 0;

#ifdef MATH_PREFIX_QRY_JOINT_NODE_METRICS
	uint64_t cur_max_qmask = 0;
	uint64_t cur_max_dmask = 0;
	uint32_t qmap[MAX_NODE_IDS] = {0};
	uint32_t dmap[MAX_NODE_IDS] = {0};
#endif

	//print_dirty_array(pq);
	counting_sort(pq);
	//print_dirty_array(pq);

	i = pq->n_dirty;
	while (i && j < k) {
		i = i - 1;
		loc = pq->dirty[i];
		cell = &pq->cell[loc.qr][loc.dr];

#ifdef MATH_PREFIX_QRY_JOINT_NODE_METRICS
		uint64_t overlap_qmask = cur_max_qmask & cell->qmask;
		uint64_t overlap_dmask = cur_max_dmask & cell->dmask;
		if (overlap_qmask && overlap_dmask) {
			if (qmap[loc.qr] == 0 && dmap[loc.dr] == 0) {
				qmap[loc.qr] = loc.dr;
				dmap[loc.dr] = loc.qr;
				n_joint_nodes ++;
#ifdef MATH_PREFIX_QRY_DEBUG_PRINT
				printf("qr%u <-> dr%u \n", loc.qr, loc.dr);
#endif
			}
#ifdef MATH_PREFIX_QRY_DEBUG_PRINT
			else if (qmap[loc.qr] != loc.dr ||
				     dmap[loc.dr] != loc.qr) {
				printf("qr%u >-< dr%u \n", loc.qr, loc.dr);
			}
#endif
		}
#endif

		if (exclude_qmask & cell->qmask || exclude_dmask & cell->dmask) {
			/* intersect */
			continue;
		} else {
			/* greedily assume the two max-subtrees match */
#ifdef MATH_PREFIX_QRY_DEBUG_PRINT
			printf("max_cell[%u](qr%u, dr%u) = %u\n", j, loc.qr, loc.dr, cell->cnt);
			printf("qr%u <-> dr%u \n", loc.qr, loc.dr);
#endif
			topk[j] = cell->cnt;
			rmap[j].qr = loc.qr;
			rmap[j].dr = loc.dr;
			j++;
			exclude_qmask |= cell->qmask;
			exclude_dmask |= cell->dmask;
#ifdef MATH_PREFIX_QRY_JOINT_NODE_METRICS
			qmap[loc.qr] = loc.dr;
			dmap[loc.dr] = loc.qr;
			n_joint_nodes ++;

			cur_max_qmask = cell->qmask;
			cur_max_dmask = cell->dmask;
#endif
		}
	}

#ifdef MATH_PREFIX_QRY_DEBUG_PRINT
	for (i = 0; i < k; i++) {
		printf("top[%u] = %u \n", i, topk[i]);
	}
	printf("joint nodes = %u \n", n_joint_nodes);
#endif
	return n_joint_nodes;
}