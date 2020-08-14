#ifndef INODE_MAP_H_INCLUDED
#define INODE_MAP_H_INCLUDED

#include "myfs.h"

struct inode_map_node_t
{
	uint32_t key;
	uint32_t inode_num;
	struct inode_t inode;
	struct inode_map_node_t *prev, *next;
};

struct inode_map_t
{
	struct inode_map_node_t **nodes;
};

void inode_map_initialize(struct inode_map_t *im);
void inode_map_destroy(struct inode_map_t *im);
void inode_map_insert(struct inode_map_t *im, uint32_t key, uint32_t inode_num, const struct inode_t *inode);
void inode_map_remove(struct inode_map_t *im, uint32_t key);
int inode_map_get(const struct inode_map_t *im, uint32_t key, uint32_t *inode_num, struct inode_t **inode);

#endif
