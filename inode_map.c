#include "inode_map.h"
#include "asserts.h"

#include <stdlib.h>

#define TABLE_SIZE 1024

void inode_map_initialize(struct inode_map_t *im)
{
	im->nodes = (struct inode_map_node_t **)malloc(TABLE_SIZE * sizeof(struct inode_map_node_t *));
	for (uint32_t i = 0; i < TABLE_SIZE; ++i)
		im->nodes[i] = NULL;
}

void inode_map_destroy(struct inode_map_t *im)
{
	for (uint32_t i = 0; i < TABLE_SIZE; ++i) {
		struct inode_map_node_t *node = im->nodes[i];
		while (node) {
			struct inode_map_node_t *n = node->next;
			free(node);
			node = n;
		}
	}
	free(im->nodes);
}

void inode_map_insert(struct inode_map_t *im, uint32_t key, uint32_t inode_num, const struct inode_t *inode)
{
	uint32_t hash = (key % TABLE_SIZE);

	struct inode_map_node_t *new_node = (struct inode_map_node_t *)malloc(sizeof(struct inode_map_node_t));
	new_node->key = key;
	new_node->inode_num = inode_num;
	new_node->inode = *inode;
	new_node->prev = new_node->next = NULL;

	struct inode_map_node_t *node = im->nodes[hash];
	if (node == NULL) {
		im->nodes[hash] = new_node;
		return;
	}
	struct inode_map_node_t *next = node->next;
	while (next) {
		node = next;
		next = next->next;
	}
	node->next = new_node;
}

void inode_map_remove(struct inode_map_t *im, uint32_t key)
{
	uint32_t hash = (key % TABLE_SIZE);

	struct inode_map_node_t *node = im->nodes[hash];
	EXPECT(node);

	while (node) {
		if (node->key == key) {
			if (node->prev) {
				node->prev->next = node->next;
			} else {
				im->nodes[hash] = node->next;
			}

			if (node->next)
				node->next->prev = node->prev;

			free(node);
			return;
		}
	}
}

int inode_map_get(const struct inode_map_t *im, uint32_t key, uint32_t *inode_num, struct inode_t **inode)
{
	uint32_t hash = (key % TABLE_SIZE);

	struct inode_map_node_t *node = im->nodes[hash];
	EXPECT(node);

	while (node) {
		if (node->key == key) {
			*inode_num = node->inode_num;
			*inode = &(node->inode);
			return 1;
		}
		node = node->next;
	}

	return 0;
}
