/* 
 * File:   types.h
 * Author: dhana
 *
 * Created on January 29, 2016, 1:35 PM
 */

#ifndef TYPES_H
#define	TYPES_H

#include <stdlib.h>
#include <assert.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef	char		int8;
typedef	short		int16;
typedef int		int32;
typedef long		int64;
typedef unsigned char	uint8;
typedef	unsigned short	uint16;
typedef unsigned int	uint32;
typedef	unsigned long	uint64;

#define SUCCESS	0
#define FAILURE	-1

#ifdef	__cplusplus
}
#endif

#endif	/* TYPES_H */

