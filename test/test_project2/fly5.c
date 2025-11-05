#include <stdio.h>
#include <unistd.h>
// #include <kernel.h>

/**
 * The ascii airplane is designed by Joan Stark
 * from: https://www.asciiart.eu/vehicles/airplanes
 */

#define CYCLE_PER_MOVE 50
#define LENGTH 60
#define CHECK_POINT 50
static char blank[] = {"                                                                               "};
static char plane1[] = {"    \\\\   "};
static char plane2[] = {" \\====== "};
static char plane3[] = {"    //   "};

int main(void)
{
    int j = 17;
    int remain_length;
    sys_set_checkpoint(CHECK_POINT);
    
    while (1)
    {
        int clk = sys_get_tick();
        remain_length = LENGTH;
        sys_set_sche_workload(remain_length);

	sys_move_cursor(CHECK_POINT + 8, j);
	printf("%c", '|');
	sys_move_cursor(CHECK_POINT + 8, j + 1);
	printf("%c", '|');
	sys_move_cursor(CHECK_POINT + 8, j + 2);
	printf("%c", '|');

        for (int i = (60 - LENGTH) * CYCLE_PER_MOVE; i < 60 * CYCLE_PER_MOVE; i++)
        {
            /* move */
            if(i % CYCLE_PER_MOVE == 0)
            {
                sys_move_cursor(i/CYCLE_PER_MOVE, j + 0);
                printf("%s", plane1);

                sys_move_cursor(i/CYCLE_PER_MOVE, j + 1);
                printf("%s", plane2);

                sys_move_cursor(i/CYCLE_PER_MOVE, j + 2);
                printf("%s", plane3);
                // sys_yield();
                // for (int j=0;j<200000;j++); // wait
                if (remain_length) remain_length--;
                sys_set_sche_workload(remain_length);
            }
        }
        // sys_yield();
        sys_move_cursor(0, j);
        printf("%s", blank);
        sys_move_cursor(0, j + 1);
        printf("%s", blank);
        sys_move_cursor(0, j + 2);
        printf("%s", blank);

        clk = sys_get_tick() - clk;
        sys_move_cursor(0, 24);
        printf("[fly5] used time per round: %d tick.",clk);
    }
}