// A process that only exists to be found in /proc: argv[0] says Xwayland,
// argv[1..] look like its arguments, and it sleeps until killed. Used by
// tst_x11session, which renames a copy of it to "Xwayland".
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (;;) {
        pause();
    }
}
