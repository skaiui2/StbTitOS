#ifndef PORT_H
#define PORT_H

#include "schedule.h"

uint32_t  EnterCritical( void );
void ExitCritical( uint32_t xReturn );
void StartFirstTask(void);
uint32_t *StackInit( uint32_t *pxTopOfStack, TaskFunction_t pxCode,void *pvParameters);

#define schedule()\
*( ( volatile uint32_t * ) 0xe000ed04 ) = 1UL << 28UL




#endif


