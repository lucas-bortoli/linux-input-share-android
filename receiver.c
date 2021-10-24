#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <stdio.h>		//printf
#include <string.h>		//strlen
#include <sys/socket.h> //socket
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>

#define SERIALIZED_PACKET_SIZE 32
#define PORT 1237

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

struct Event *parseEvent(uint16_t serialized[SERIALIZED_PACKET_SIZE])
{
	struct Event *str = malloc(sizeof(struct Event));
	str->type = serialized[0];
	str->kernel_event.type = (unsigned short)serialized[1];
	str->kernel_event.code = (unsigned short)serialized[2];
	str->kernel_event.value = (unsigned int)serialized[3];
	return str;
}

void write_event(int fd, int evtype, int evcode, int evvalue)
{
	struct input_event event;
	event.type = evtype;
	event.code = evcode;
	event.value = evvalue;
	write(fd, &event, sizeof(event));
}

void write_syn(int fd)
{
	struct input_event event;
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	write(fd, &event, sizeof(event));
}

int main(int argc, char **argv)
{
	int sock;
	struct sockaddr_in server;

	// Create socket
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1)
	{
		printf("Could not create socket\n");
	}

	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_family = AF_INET;
	server.sin_port = htons(PORT);

	// Connect to server
	if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		printf("Connect failed.\n");
		return 1;
	}

	printf("Connected\n");

	// open keyboard
	int kbd_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	struct uinput_user_dev kbd_usetup;
	memset(&kbd_usetup, 0, sizeof(struct uinput_user_dev));
	strcpy(kbd_usetup.name, "Virtual Keyboard Device");
	kbd_usetup.id.version = 4;
	kbd_usetup.id.bustype = BUS_USB;
	kbd_usetup.id.product = 1;
	kbd_usetup.id.vendor = 1;

	ioctl(kbd_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(kbd_fd, UI_SET_EVBIT, EV_REL);

	for (int i = 1; i < 0xFF; i++)
	{
		// register keys
		ioctl(kbd_fd, UI_SET_KEYBIT, i);
	}

	write(kbd_fd, &kbd_usetup, sizeof(kbd_usetup));
	ioctl(kbd_fd, UI_DEV_CREATE);

	printf("Virtual keyboard device set up\n");

	// open mouse
	struct uinput_user_dev mice_setup;
	int mice_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	// enable mouse button left and relative events
	ioctl(mice_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(mice_fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(mice_fd, UI_SET_KEYBIT, BTN_MIDDLE);
	ioctl(mice_fd, UI_SET_KEYBIT, BTN_RIGHT);

	ioctl(mice_fd, UI_SET_EVBIT, EV_REL);
	ioctl(mice_fd, UI_SET_RELBIT, REL_X);
	ioctl(mice_fd, UI_SET_RELBIT, REL_Y);
	ioctl(mice_fd, UI_SET_RELBIT, REL_WHEEL);

	memset(&mice_setup, 0, sizeof(mice_setup));
	mice_setup.id.bustype = BUS_USB;
	mice_setup.id.vendor = 0x1234;
	mice_setup.id.product = 0x5678;
	strcpy(mice_setup.name, "Virtual mouse device");

	write(mice_fd, &mice_setup, sizeof(mice_setup));
	ioctl(mice_fd, UI_DEV_CREATE);

	printf("Virtual mouse device set up\n");

	uint16_t packet[SERIALIZED_PACKET_SIZE];
	int left, middle, right;
	int scroll;
	signed char x, y;

	while (1)
	{
		// if (recv(sock, &evt, sizeof(struct Event), MSG_WAITALL) < 0)
		if (recv(sock, packet, SERIALIZED_PACKET_SIZE * sizeof(uint16_t), MSG_WAITALL) < 0)
		{
			printf("Packet receive failed!\n");
			break;
		}

		struct Event *ev = parseEvent(packet);

		if (ev->type == MOUSE_EVENT)
		{
			if (ev->kernel_event.code == BTN_LEFT)
			{
				write_event(mice_fd, EV_KEY, BTN_LEFT, (signed char)ev->kernel_event.value);
			}
			else if (ev->kernel_event.code == BTN_MIDDLE)
			{
				write_event(mice_fd, EV_KEY, BTN_MIDDLE, (signed char)ev->kernel_event.value);
			}
			else if (ev->kernel_event.code == BTN_RIGHT)
			{
				write_event(mice_fd, EV_KEY, BTN_RIGHT, (signed char)ev->kernel_event.value);
			}
			else
			{
				write_event(mice_fd, ev->kernel_event.type, (unsigned char)ev->kernel_event.code, (signed char)ev->kernel_event.value);
			}
		}
		else
		{
			// send keyboard event
			write_event(kbd_fd, ev->kernel_event.type, ev->kernel_event.code, ev->kernel_event.value);
		}

		// clean up (malloc'ed in parseEvent())
		free(ev);
	}

	// close device & socket handles
	ioctl(kbd_fd, UI_DEV_DESTROY);
	ioctl(mice_fd, UI_DEV_DESTROY);
	close(sock);
	close(mice_fd);
	close(kbd_fd);

	return 0;
}
