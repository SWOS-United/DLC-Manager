/* startup.c -- program entry, in place of a C runtime's startup code.
 *
 * AmigaDOS starts a program at the first byte of its first hunk. The linker
 * puts .text.unlikely ahead of the rest of .text and the Makefile links this
 * file first, so _start is that first byte.
 *
 * Started from Workbench, the process must take the WBStartup message before
 * doing anything else and reply to it under Forbid() as the very last thing,
 * or Workbench unloads the code while it is still running.
 */
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <proto/exec.h>

struct ExecBase *SysBase;
struct WBStartup *g_wb_startup;

int main(void);

__attribute__((used, section(".text.unlikely"))) int _start(void)
{
    struct Process *me;
    int rc;

    SysBase = *(struct ExecBase **)4UL;
    me = (struct Process *)FindTask(NULL);

    g_wb_startup = NULL;
    if (!me->pr_CLI) {
        WaitPort(&me->pr_MsgPort);
        g_wb_startup = (struct WBStartup *)GetMsg(&me->pr_MsgPort);
    }

    rc = main();

    if (g_wb_startup) {
        Forbid();
        ReplyMsg((struct Message *)g_wb_startup);
    }
    return rc;
}
