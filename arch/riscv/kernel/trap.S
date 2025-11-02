#include <asm.h>
#include <csr.h>

    ENTRY(setup_exception)

/* save exception_handler_entry into STVEC */
    la                       t0, exception_handler_entry
    csrw                     stvec, t0
/* TODO:
    [p2-task4]               enable interrupts globally */

    ret
    ENDPROC(setup_exception)
