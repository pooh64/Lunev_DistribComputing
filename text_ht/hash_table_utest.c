#include "hash_table.h"

int main()
{
	hash_table_t *ht = hash_table_new(1024);
	hash_table_delete(ht);
	return 0;
}
