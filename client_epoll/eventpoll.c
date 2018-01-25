#include <sys/queue.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <assert.h>

/*----------------------------------------------------------------------------*/
char *event_str[] = {"ONESHOT", "IN", "PRI", "OUT", "ERR", "HUP", "ET"};
/*----------------------------------------------------------------------------*/
char *
EventToString(uint32_t event)
{
	switch (event) {
		case EPOLLONESHOT:
			return event_str[0];
			break;
		case EPOLLIN:
			return event_str[1];
			break;
		case EPOLLPRI:
			return event_str[2];
			break;
		case EPOLLOUT:
			return event_str[3];
			break;
		case EPOLLERR:
			return event_str[4];
			break;
		case EPOLLHUP:
			return event_str[5];
			break;
		case EPOLLET:
			return event_str[6];
			break;
		default:
			assert(0);
	}

	assert(0);
	return NULL;
}
