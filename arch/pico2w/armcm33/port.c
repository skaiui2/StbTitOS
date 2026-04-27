
#include "port.h"
#include "config.h"
#include "timer.h"


struct Stack_register {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    // automatic stacking 
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t LR;
    uint32_t PC;
    uint32_t xPSR;
};


void ErrorHandle(void)
{
    while (1){

    }
}


#define INITIAL_XPSR        0x01000000UL      // Thumb bit 
#define INITIAL_EXC_RETURN  0xFFFFFFFDUL      // Thread, PSP, no FP 

uint32_t * StackInit(uint32_t *pxTopOfStack,
                     TaskFunction_t pxCode,
                     void *pvParameters)
{
    pxTopOfStack -= sizeof(struct Stack_register) / sizeof(uint32_t);
    struct Stack_register *frame = (struct Stack_register *)pxTopOfStack;

    *frame = (struct Stack_register){
        .r4   = 0,
        .r5   = 0,
        .r6   = 0,
        .r7   = 0,
        .r8   = 0,
        .r9   = 0,
        .r10  = 0,
        .r11  = 0,
        .r0   = (uint32_t)pvParameters,
        .r1   = 0,
        .r2   = 0,
        .r3   = 0,
        .r12  = 0,
        .LR   = (uint32_t)ErrorHandle,              
        
        .PC   = ( ( uint32_t ) pxCode ) & ( ( uint32_t ) 0xfffffffeUL ),
        .xPSR = INITIAL_XPSR
    };

    pxTopOfStack--;
    *pxTopOfStack = INITIAL_EXC_RETURN;

    //PSPLIM 
    pxTopOfStack--;
    *pxTopOfStack = (uint32_t)0;

    return pxTopOfStack;
}



extern TaskHandle_t volatile schedule_currentTCB;
__attribute__ (( naked ))    void vRestoreContextOfFirstTask( void )
    {
        __asm volatile
        (
            "   .syntax unified                                 \n"
            "                                                   \n"
            "   ldr  r2, =schedule_currentTCB                          \n" 
            "   ldr  r1, [r2]                                   \n"

            "   ldr  r0, [r1]                                   \n" 
            "                                                   \n"
            "   ldm  r0!, {r1-r2}                               \n" 
            "   msr  psplim, r1                                 \n" 
            "   movs r1, #2                                     \n" 
            "   msr  CONTROL, r1                                \n"
            "   adds r0, #32                                    \n" 
            "   msr  psp, r0                                    \n"
            "   isb                                             \n"
            "   mov  r0, #0                                     \n"
            "   msr  basepri, r0                                \n" 
            "   bx   r2                                         \n" 
        );
    }


extern void task_switch_context(void);
 __attribute__ (( naked ))   void PendSV_Handler( void ) 
{
        __asm volatile
        (
            "   .syntax unified                                 \n"
            "                                                   \n"
            "   mrs r1, psp                                     \n" 
            "                                                   \n"
            "                                                   \n"
            "   mrs r2, psplim                                  \n" 
            "   mov r3, lr                                      \n" 
            "   stmdb r1!, {r2-r11}                             \n" 
            "                                                   \n"

            "   ldr r2, =schedule_currentTCB                          \n" 
            "   ldr r3, [r2]                                    \n" 

            "   str r1, [r3]                                    \n" 
            "                                                   \n"
            "   mov r1, %0                                      \n" 
            "   msr basepri, r1                                 \n" 
            "   dsb                                             \n"
            "   isb                                             \n"
            "   bl task_switch_context                          \n"
            "   mov r0, #0                                      \n" 
            "   msr basepri, r0                                 \n" 
            "                                                   \n"
         
            "   ldr r2, =schedule_currentTCB                           \n" 
            "   ldr r1, [r2]                                    \n" 

            "   ldr r0, [r1]                                    \n" 
            "                                                   \n"


            "   ldmia r0!, {r2-r11}                             \n" 
            "                                                   \n"

            "                                                   \n"
            "   msr psplim, r2                                  \n" 
            "   msr psp, r0                                     \n" 
            "   bx r3                                           \n"

            ::"i" (configShieldInterPriority)
        );
}

 __attribute__ (( naked ))  void SVC_Handler( void ) 
{
    __asm volatile
    (
            "   .syntax unified                                 \n"
            "                                                   \n"
            "   tst lr, #4                                      \n"
            "   ite eq                                          \n"
            "   mrseq r0, msp                                   \n"
            "   mrsne r0, psp                                   \n"
            "   ldr r1, =vRestoreContextOfFirstTask                      \n"
            "   bx r1                                           \n"
        );
}


struct SysTicks {
    uint32_t CTRL;
    uint32_t LOAD;
    uint32_t VAL;
    uint32_t CALIB;
};

 void StartFirstTask(void)
{
    ( *( ( volatile uint32_t * ) 0xe000ed20 ) ) |= ( ( ( uint32_t ) 255UL ) << 16UL );
    ( *( ( volatile uint32_t * ) 0xe000ed20 ) ) |= ( ( ( uint32_t ) 255UL ) << 24UL );

    struct SysTicks *SysTick = (struct SysTicks * volatile)0xe000e010;

    /* Configure SysTick to interrupt at the requested rate. */
    *SysTick = (struct SysTicks){
            .LOAD = ( configSysTickClockHz / configTickRateHz ) - 1UL,
            .CTRL  = ( ( 1UL << 2UL ) | ( 1UL << 1UL ) | ( 1UL << 0UL ) )
    };

    __asm volatile (
            " ldr r0, =0xE000ED08 	\n"/* Use the NVIC offset register to locate the stack. */
            " ldr r0, [r0] 			\n"
            " ldr r0, [r0] 			\n"
            " msr msp, r0			\n"/* Set the msp back to the start of the stack. */
            " cpsie i				\n"/* Globally enable interrupts. */
            " cpsie f				\n"
            " dsb					\n"
            " isb					\n"
            " svc 0					\n"/* System call to start first task. */
            " nop					\n"//wait
            " .ltorg				\n"
            );
}


__attribute__((always_inline)) inline uint32_t EnterCritical(void)
{
    uint32_t xReturn;

    __asm volatile(
        " mrs %0, basepri      \n"  
        " msr basepri, %1      \n"  
        " dsb                  \n"
        " isb                  \n"
        : "=r"(xReturn)
        : "r"(configShieldInterPriority)
        : "memory"
    );

    return xReturn;
}

__attribute__((always_inline)) inline void ExitCritical(uint32_t xReturn)
{
    __asm volatile(
        " msr basepri, %0      \n" 
        " dsb                  \n"
        " isb                  \n"
        :: "r"(xReturn)
        : "memory"
    );
}

void SysTick_Handler(void)
{
    uint32_t lock = EnterCritical();
    timer_tick();
    check_ticks();
    ExitCritical(lock);
}