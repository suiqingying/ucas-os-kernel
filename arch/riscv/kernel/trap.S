#include <asm.h>
#include <csr.h>

    ENTRY(setup_exception)

/* save exception_handler_entry into STVEC */
    la                       t0, exception_handler_entry
    csrw                     stvec, t0
    csrw    sip, zero
/* enable interrupts globally */
    csrs sstatus, SR_SIE
    ret
    ENDPROC(setup_exception)
