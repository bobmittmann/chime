#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int __key_lookup(struct key_list * lst, unsigned int key)
{
	int imin = 0;
	int imax = lst->cnt - 1;

	if (imax < 0)
		return -1;

	while (imin < imax) {
		int imid = (imin + imax) / 2;

		if (lst->key[imid] < key) 
			imin = imid + 1;
		else
			imax = imid;
	}

	if ((imax == imin) && (lst->key[imin] == key))
		return imin;

	return -1;
}


void key_delete(struct key_list * lst, unsigned int idx)
{
	int i;

	assert(idx < lst->cnt);

	lst->cnt--;

	for (i = idx; i < lst->cnt; ++i) {
		lst->key[i] = lst->key[i + 1];
	}
}

static int __key_next(struct key_list * lst, unsigned int key)
{
	int imin = 0;
	int imax = lst->cnt - 1;

	if (imax < 0)
		return -1;

	if (key >= lst->key[imax])
		return -1;

	while (imin < imax) {
		int imid = (imin + imax) / 2;

		if (lst->key[imid] < key) 
			imin = imid + 1;
		else
			imax = imid;
	}

	if (lst->key[imin] <= key)
		return imin + 1;

	return imin;
}

void key_list_insert(struct key_list * lst, unsigned int key)
{
	int idx;
	int i;

	idx = __key_next(lst, key);

	if (idx < 0) {
		/* Append */
		lst->key[lst->cnt++] = key;
		return;
	}

	/* Make room for the new key. Move keys by one position */
	for (i = lst->cnt; i > idx; --i)
		lst->key[i] = lst->key[i - 1];
	lst->cnt++;

	lst->key[idx] = key;
}

bool key_list_contains(struct key_list * lst, unsigned int key);
{
	return __key_lookup(lst, key) < 0 ? false : true;
}

int key_list_indexof(struct key_list * lst, unsigned int key);
{
	return __key_lookup(lst, key);
}

void key_list_remove(struct key_list * lst, unsigned int key)
{
	int idx;

	idx = __key_lookup(lst, key);

	assert(idx >= 0);

	key_delete(lst, idx);
}

void key_list_dump(FILE * f,  struct key_list * lst)
{
	int i;

	fprintf(f, "-");
	for (i = 0; i < lst->cnt; ++i) {
		fprintf(f, " %02x", lst->key[i]);
	}

	fprintf(f, "\n");
	fflush(f);
}

void key_list_init(struct key_list * lst)
{
	lst->cnt = 0;
}

