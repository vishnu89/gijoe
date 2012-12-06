#include "disksim_global.h"
#include "adivim.h"
#include "../ssdmodel/ssd_utils.h" // For listnode.

listnode** hash_table;

typedef struct _section
{
	int blk_no;
	int type; /* 1 : HOT, 0 : COLD */
}section;

/////////////////////////////////////////////////////////
/* In ssd_init.c */
extern listnode** hash_table;


/* In ssd_init.c - ssd_element_metadata_init */

hash_table = (listnode**) malloc(512*sizeof(listnode*));

////////////////////////////////////////////////////////

void hash_init(listnode** hash)
{
	int i;
	
	for(i=0; i < 512 ; i++)
	{
		li_create(&hash[i]);
	}

	return;
}

//현재 해쉬 테이블에 없는 원소만 넣기
void hash_insert(listnode** hash, int blk_no, int type)
{
	int key;
	section* elem;

	key = blk_no & 511;

	elem = (section*)malloc(sizeof(section));

	elem->blk_no = blk_no;
	elem->type = type;

	ll_insert_at_tail(hash[key], (void*)elem);

	return;
}

//현재 해쉬 테이블에 해당 원소가 있는지 확인
int hash_find(listnode** hash, int blk_no)
{
	int i, key, size, value;
	section* elem;

	key = blk_no & 511;

	size = ll_get_size(hash[key]);
	
	for(i=0; i<size;i++)
	{
		elem = (section*)(ll_get_nth_node(hash[key], i)->data);
		if(elem->blk_no == blk_no)
		{
			value = elem->type;
			break;
		}
	}
	
	if(i == size)
	{
		value = -1;
	}

	return value;
}

void hash_remove(listnode** hash, int blk_no)
{
	int i, key, size, value;
	section* elem;
	listnode* node;

	key = blk_no & 511;
	
	size = ll_get_size(hash[key]);
	
	for(i=0; i<size;i++)
	{
		node = ll_get_nth_node(hash[key], i);
		elem = (section*)(node->data);
		if(elem->blk_no == blk_no)
		{
			ll_release_node(hash[key], node);
			break;
		}
	}
	
	if(i == size)
	{
		fprintf(stderr, "Error (from remove) : no target item in hash table\n");
	}
	
	free(elem);
	return;
}

void hash_replace(listnode** hash, int blk_no, int type)
{
	int i, key, size, value;
	section* elem;
	listnode* node;

	key = blk_no & 511;
	
	size = ll_get_size(hash[key]);
	
	for(i=0; i<size;i++)
	{
		node = ll_get_nth_node(hash[key], i);
		elem = (section*)(node->data);
		if(elem->blk_no == blk_no)
		{
			elem->type = type;
		}
	}
	
	if(i == size)
	{
		fprintf(stderr, "Error (from replace) : no target item in hash table\n");
	}
	
	return;
}
