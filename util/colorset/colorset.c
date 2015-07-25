#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>

#include "procutils.h"


struct colorset {
	pid_t		pid;		/* task PID */
	cpu_set_t	*set;		/* task color mask */
	size_t		setsize;
	char		*buf;		/* buffer for conversion from mask to string */
	size_t		buflen;
	unsigned int	use_list:1,	/* use list rather than masks */
			get_only:1;	/* print the mask, but not modify */
};

static void usage(FILE* out)
{
	fprintf(out,"Usage: ./colorset [options] [pid | cmd [args...]]\n\n");
	fprintf(out, "Options:\n"
		" -p, operate on existing given pid\n\n");

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);

}

static void print_color(struct colorset *cs, int isnew)
{
}

/*
static void do_colorset(struct colorset *cs, size_t setsize, cpu_set_t *set)
{
	if ( set_color(cs->pid, setsize, set) < 0 ) {
		printf("err\n");
	}

	if ( cs->pid ) {
		if (get_color(cs->pid, cs->setsize, cs->set) < 0 ){
			printf("err\n");	
		}

		print_color(cs, TRUE);
	}
}
*/

int main(int argc, char **argv)
{
	int c;
	pid_t pid = 0;
	struct colorset cs;

	while ((c = getopt(argc, argv, "ph")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(argv[argc - 1]);
			break;
		case 'h':
			usage(stdout);
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if ((!pid && argc - optind < 2)
	    || (pid && (argc - optind < 1 || argc - optind > 2)))
		usage(stderr);

	if (pid) {
		
		struct proc_tasks *tasks = proc_open_tasks(pid);
		while (!proc_next_tid(tasks, &pid)) {
			// do_colorset(&cs, new_setsize, new_set);
		}
		proc_close_tasks(tasks);

	} else {
		cs.pid = pid;
		// do_colorset(&cs, new_setsize, new_set);
	}
	
	free(cs.buf);

	printf("optind: %d\n", optind);
	
	if (!pid) {
		argv += optind ;
		printf ("%s\n", argv[0]);
		execvp(argv[0], argv);
	}
	


	return EXIT_SUCCESS;
}
