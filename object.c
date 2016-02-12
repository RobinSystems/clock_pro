#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"

int
create_object(char *id, struct object **robjp)
{
	struct object *obj = (struct object *) malloc (sizeof(struct object));
	if (!obj) {
		return FAILURE;
	}
	obj->id = strdup(id);

	printf("\nCreated object %p: %s\n", obj, obj->id);
	*robjp = obj;
	return SUCCESS;
}

int
destroy_object(struct object *obj)
{
	printf("Destroying object %p: %s\n", obj, obj->id);
	free(obj->id);
	obj->id = NULL;
	free(obj);
	return SUCCESS;
}

int
compare_object(struct object *obj1, struct object *obj2)
{
	return strcmp(obj1->id, obj2->id);
}

void
print_object(struct object *obj)
{
	printf("%s", obj->id);
}