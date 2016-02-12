/* 
 * File:   object.h
 * Author: dhana
 *
 * Created on January 29, 2016, 1:33 PM
 */

#ifndef OBJECT_H
#define	OBJECT_H

#include "types.h"
#ifdef	__cplusplus
extern "C" {
#endif

struct object {
	char	*id;
};
int create_object(char *id, struct object **robjp);
int destroy_object(struct object *obj);
int compare_object(struct object *obj1, struct object *obj2);
void print_object(struct object *obj);




#ifdef	__cplusplus
}
#endif

#endif	/* OBJECT_H */

