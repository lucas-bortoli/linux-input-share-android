#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include <dirent.h> // scandir

#define PORT 1237

// find these values with `evtest`
#define MOUSE_EVDEV_DEVICE "/dev/input/event11"
#define KEYB_EVDEV_DEVICE "/dev/input/event4"

// find these values with `xinput list`
#define MOUSE_X11_DEVICE_ID 11
#define KEYBOARD_X11_DEVICE_ID 14

#define SERIALIZED_PACKET_SIZE 32 // don't touch this

enum EventType
{
	KEYBOARD_EVENT,
	MOUSE_EVENT
};

struct Event
{
	enum EventType type;
	struct input_event kernel_event;
};

struct ThreadArgument
{
	int socket_fd;
	suseconds_t last_lock_time;

	// While it's locked, only the phone can receive input events
	// While it's unlocked, only pc can
	bool *input_lock;
};

void toggle_x11_input(bool on)
{
	char *command = malloc(sizeof(char) * 64);

	memset(command, 0, sizeof(char) * 64);
	sprintf(command, "xinput set-prop %i \"Device Enabled\" %i", MOUSE_X11_DEVICE_ID, on ? 1 : 0);
	system(command);

	sprintf(command, "xinput set-prop %i \"Device Enabled\" %i", KEYBOARD_X11_DEVICE_ID, on ? 1 : 0);
	system(command);

	free(command);
}

unsigned char *serializeEvent(struct Event *ev)
{
	uint16_t serialized[] = {ev->type, ev->kernel_event.type, ev->kernel_event.code, ev->kernel_event.value};
	unsigned char *data = malloc(SERIALIZED_PACKET_SIZE * sizeof(uint16_t));
	memset(data, 0, SERIALIZED_PACKET_SIZE * sizeof(uint16_t));
	memcpy(data, serialized, sizeof(serialized));
	return data;
}

void *mouseEventThread(void *thread_arg)
{
	printf("Starting mouse event thread\n");

	struct ThreadArgument arg = *((struct ThreadArgument *)thread_arg);

	int socket = arg.socket_fd;
	int fd, bytes;

	const char *pDevice = MOUSE_EVDEV_DEVICE;

	// open mouse
	fd = open(pDevice, O_RDWR);
	if (fd == -1)
	{
		printf("(mouse thread) ERROR Opening %s\n", pDevice);
		return NULL;
	}

	struct input_event kev;
	struct Event ev;

	while (1)
	{
		memset(&ev, 0, sizeof(struct Event));

		// read mouse events
		bytes = read(fd, &kev, sizeof(struct input_event));

		if (bytes > 0)
		{
			ev.type = MOUSE_EVENT;
			ev.kernel_event = kev;

			// is mouse locked?
			if (*arg.input_lock)
				continue;

			unsigned char *serialized = serializeEvent(&ev);

			//printf("Sending mouse event (type=%i)\n", ev.type);

			// send mouse event over socket
			int bytes_written = write(socket, serialized, SERIALIZED_PACKET_SIZE * sizeof(uint16_t));

			free(serialized);

			if (bytes_written <= 0) {
				// uh oh broken pipe
				toggle_x11_input(true);
				break;
			}

		}
	}
}

void *keyboardEventThread(void *thread_arg)
{
	printf("Starting keyboard event thread\n");

	struct ThreadArgument arg = *((struct ThreadArgument *)thread_arg);

	int socket = arg.socket_fd;
	int fd, bytes;

	const char *pDevice = KEYB_EVDEV_DEVICE;

	// open keyboard
	fd = open(pDevice, O_RDWR);
	if (fd == -1)
	{
		printf("(kbd thread) ERROR Opening %s\n", pDevice);
		return NULL;
	}

	struct input_event kev;
	struct Event ev;

	while (1)
	{
		memset(&ev, 0, sizeof(struct Event));

		// read keyboard events
		bytes = read(fd, &kev, sizeof(struct input_event));

		if (bytes > 0)
		{
			ev.type = KEYBOARD_EVENT;
			ev.kernel_event = kev;

			// is lock keyboard keycode?
			if (ev.kernel_event.code == 96)
			{
				if (ev.kernel_event.time.tv_sec - arg.last_lock_time < 1)
				{
					continue;
				}

				arg.last_lock_time = ev.kernel_event.time.tv_sec;

				// switch lock on
				*arg.input_lock = !(*arg.input_lock);

				printf("%s\n", *arg.input_lock ? "Locked" : "Unlocked");

				// enable/disable devices in x11
				toggle_x11_input(*arg.input_lock);

				continue;
			}

			// is keyboard locked?
			if (*arg.input_lock)
				continue;

			unsigned char *serialized = serializeEvent(&ev);

			// send keyboard event over socket
			int bytes_written = write(socket, serialized, SERIALIZED_PACKET_SIZE * sizeof(uint16_t));

			free(serialized);

			if (bytes_written <= 0) {
				// uh oh broken pipe
				toggle_x11_input(true);
				break;
			}

		}
	}
}

int main(int argc, char **argv)
{
	toggle_x11_input(true);

	// don't crash process on broken pipe
	signal(SIGPIPE, SIG_IGN);

	int socket_desc, client_sock, c, read_size;
	struct sockaddr_in server, client;

	// Create socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket\n");
	}
	printf("Socket created\n");

	// Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(PORT);

	// Bind
	if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		//print the error message
		perror("Bind failed. Error\n");
		return 1;
	}
	printf("Bind done\n");

	// Listen
	listen(socket_desc, 3);

	// Accept and incoming connection
	printf("Waiting for incoming connections...\n");
	c = sizeof(struct sockaddr_in);

	// Accept connection from an incoming client
	client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c);
	if (client_sock < 0)
	{
		perror("accept failed\n");
		return 1;
	}
	printf("Connection accepted\n");

	bool input_locked = true;
	struct ThreadArgument thread_arg;

	memset(&thread_arg, 0, sizeof thread_arg);
	thread_arg.last_lock_time = 0;
	thread_arg.input_lock = &input_locked;
	thread_arg.socket_fd = client_sock;

	pthread_t mouse_thread, kbd_thread;
	int mouse_thread_err, kbd_thread_err;

	// start event threads
	mouse_thread_err = pthread_create(&mouse_thread, NULL, &mouseEventThread, &thread_arg);
	kbd_thread_err = pthread_create(&kbd_thread, NULL, &keyboardEventThread, &thread_arg);

	if (mouse_thread_err != 0)
	{
		printf("mouse thread creation error %d", mouse_thread_err);
		return mouse_thread_err;
	}

	if (kbd_thread_err != 0)
	{
		printf("kbd thread creation error %d", kbd_thread_err);
		return kbd_thread_err;
	}

	// stall while thread is still running
	// this function has a bad name... if the signal is 0 (which it is), it doesn't actually
	// kill the thread, just checks if it is running
	while (pthread_kill(mouse_thread, 0) == 0 && pthread_kill(kbd_thread, 0) == 0)
	{
		/* still running */
	}

	printf("Exiting\n");
	pthread_kill(mouse_thread, 1);

	// just in case the devices are locked, unlock them
	toggle_x11_input(true);	

	return 0;
}
