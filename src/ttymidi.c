/*
    This file is part of ttymidi.

    ttymidi is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ttymidi is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ttymidi.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <argp.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
// Linux-specific
#include <linux/serial.h>
#include <linux/ioctl.h>
#include <asm/ioctls.h>

#define MAX_DEV_STR_LEN               32
#define MAX_MSG_SIZE                1024

/* change this definition for the correct port */
//#define _POSIX_SOURCE 1 /* POSIX compliant source */

bool run;
int serial;

/* --------------------------------------------------------------------- */
// Program options

static struct argp_option options[] =
{
	{"serialdevice" , 's', "DEV" , 0, "Serial device to use. Default = /dev/ttyUSB0", 0 },
	{"baudrate"     , 'b', "BAUD", 0, "Serial port baud rate. Default = 115200", 0 },
	{"verbose"      , 'v', 0     , 0, "For debugging: Produce verbose output", 0 },
	{"printonly"    , 'p', 0     , 0, "Super debugging: Print values read from serial -- and do nothing else", 0 },
	{"quiet"        , 'q', 0     , 0, "Don't produce any output, even when the print command is sent", 0 },
	{"name"		, 'n', "NAME", 0, "Name of the Alsa MIDI client. Default = ttymidi", 0 },
	{ 0 }
};

typedef struct _arguments
{
	int  silent, verbose, printonly;
	char serialdevice[MAX_DEV_STR_LEN];
	int  baudrate;
	char name[MAX_DEV_STR_LEN];
} arguments_t;

typedef struct _jackdata
{
    jack_client_t* client;
    jack_port_t* port_in;
    jack_port_t* port_out;
    jack_ringbuffer_t* ringbuffer;
} jackdata_t;

void exit_cli(int sig)
{
        run = false;
        printf("\rttymidi closing down ... ");

        // unused
        return; (void)sig;
}

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
	/* Get the input argument from argp_parse, which we
	   know is a pointer to our arguments structure. */
	arguments_t *arguments = state->input;
	int baud_temp;

	switch (key)
	{
		case 'p':
			arguments->printonly = 1;
			break;
		case 'q':
			arguments->silent = 1;
			break;
		case 'v':
			arguments->verbose = 1;
			break;
		case 's':
			if (arg == NULL) break;
			strncpy(arguments->serialdevice, arg, MAX_DEV_STR_LEN);
			break;
		case 'n':
			if (arg == NULL) break;
			strncpy(arguments->name, arg, MAX_DEV_STR_LEN);
			break;
		case 'b':
			if (arg == NULL) break;
			baud_temp = strtol(arg, NULL, 0);
			if (baud_temp != EINVAL && baud_temp != ERANGE)
				switch (baud_temp)
				{
					case 1200   : arguments->baudrate = B1200  ; break;
					case 2400   : arguments->baudrate = B2400  ; break;
					case 4800   : arguments->baudrate = B4800  ; break;
					case 9600   : arguments->baudrate = B9600  ; break;
					case 19200  : arguments->baudrate = B19200 ; break;
					case 38400  : arguments->baudrate = B38400 ; break;
					case 57600  : arguments->baudrate = B57600 ; break;
					case 115200 : arguments->baudrate = B115200; break;
					default: printf("Baud rate %i is not supported.\n",baud_temp); exit(1);
				}

		case ARGP_KEY_ARG:
		case ARGP_KEY_END:
			break;

		default:
			return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

void arg_set_defaults(arguments_t *arguments)
{
	char *serialdevice_temp = "/dev/ttyUSB0";
	arguments->printonly    = 0;
	arguments->silent       = 0;
	arguments->verbose      = 0;
	arguments->baudrate     = B115200;
	char *name_tmp		= (char *)"ttymidi";
	strncpy(arguments->serialdevice, serialdevice_temp, MAX_DEV_STR_LEN);
	strncpy(arguments->name, name_tmp, MAX_DEV_STR_LEN);
}

const char *argp_program_version     = "ttymidi 0.60";
const char *argp_program_bug_address = "tvst@hotmail.com";
static char doc[]       = "ttymidi - Connect serial port devices to ALSA MIDI programs!";
static struct argp argp = { options, parse_opt, NULL, doc, NULL, NULL, NULL };
arguments_t arguments;



/* --------------------------------------------------------------------- */
// JACK stuff

void open_client(jackdata_t* jackdata)
{
        jack_client_t *client;
        jack_port_t *port_in, *port_out;
        jack_ringbuffer_t *ringbuffer;

        if ((client = jack_client_open(arguments.name, JackNoStartServer, NULL)) == NULL)
        {
                fprintf(stderr, "Error opening JACK client.\n");
                exit(1);
        }

        if ((port_in = jack_port_register(client, "MIDI in", JACK_DEFAULT_MIDI_TYPE,
                                          JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal,
                                          0x0)) == NULL)
        {
                fprintf(stderr, "Error creating input port.\n");
        }

        if ((port_out = jack_port_register(client, "MIDI out", JACK_DEFAULT_MIDI_TYPE,
                                           JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal,
                                           0x0)) == NULL)
        {
                fprintf(stderr, "Error creating output port.\n");
        }

        if ((ringbuffer = jack_ringbuffer_create(MAX_MSG_SIZE-1)) == NULL)
        {
                fprintf(stderr, "Error creating JACK ringbuffer.\n");
        }

        if (port_in == NULL || port_out == NULL || ringbuffer == NULL)
        {
                jack_client_close(client);
                exit(1);
        }

        jackdata->client = client;
        jackdata->port_in = port_in;
        jackdata->port_out = port_out;
        jackdata->ringbuffer = ringbuffer;

        if (jack_activate(client) != 0)
        {
                fprintf(stderr, "Error activating JACK client.\n");
                jack_client_close(client);
                exit(1);
        }

        jack_ringbuffer_mlock(ringbuffer);
}

void close_client(jackdata_t* jackdata)
{
        jack_deactivate(jackdata->client);
        jack_port_unregister(jackdata->client, jackdata->port_in);
        jack_port_unregister(jackdata->client, jackdata->port_out);
        jack_ringbuffer_free(jackdata->ringbuffer);
        jack_client_close(jackdata->client);
        bzero(jackdata, sizeof(*jackdata));
}

/* --------------------------------------------------------------------- */
// MIDI stuff

#if 0
void parse_midi_command(snd_seq_t* seq, int port_out_id, char *buf)
{
	/*
	   MIDI COMMANDS
	   -------------------------------------------------------------------
	   name                 status      param 1          param 2
	   -------------------------------------------------------------------
	   note off             0x80+C       key #            velocity
	   note on              0x90+C       key #            velocity
	   poly key pressure    0xA0+C       key #            pressure value
	   control change       0xB0+C       control #        control value
	   program change       0xC0+C       program #        --
	   mono key pressure    0xD0+C       pressure value   --
	   pitch bend           0xE0+C       range (LSB)      range (MSB)
	   system               0xF0+C       manufacturer     model
	   -------------------------------------------------------------------
	   C is the channel number, from 0 to 15;
	   -------------------------------------------------------------------
	   source: http://ftp.ec.vanderbilt.edu/computermusic/musc216site/MIDI.Commands.html

	   In this program the pitch bend range will be transmitter as
	   one single 8-bit number. So the end result is that MIDI commands
	   will be transmitted as 3 bytes, starting with the operation byte:

	   buf[0] --> operation/channel
	   buf[1] --> param1
	   buf[2] --> param2        (param2 not transmitted on program change or key press)
   */

	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);
	snd_seq_ev_set_direct(&ev);
	snd_seq_ev_set_source(&ev, port_out_id);
	snd_seq_ev_set_subs(&ev);

	int operation, channel, param1, param2;

	operation = buf[0] & 0xF0;
	channel   = buf[0] & 0x0F;
	param1    = buf[1];
	param2    = buf[2];

	switch (operation)
	{
		case 0x80:
			if (!arguments.silent && arguments.verbose)
				printf("Serial  0x%x Note off           %03u %03u %03u\n", operation, channel, param1, param2);
			snd_seq_ev_set_noteoff(&ev, channel, param1, param2);
			break;

		case 0x90:
			if (!arguments.silent && arguments.verbose)
				printf("Serial  0x%x Note on            %03u %03u %03u\n", operation, channel, param1, param2);
			snd_seq_ev_set_noteon(&ev, channel, param1, param2);
			break;

		case 0xA0:
			if (!arguments.silent && arguments.verbose)
				printf("Serial  0x%x Pressure change    %03u %03u %03u\n", operation, channel, param1, param2);
			snd_seq_ev_set_keypress(&ev, channel, param1, param2);
			break;

		case 0xB0:
			if (!arguments.silent && arguments.verbose)
				printf("Serial  0x%x Controller change  %03u %03u %03u\n", operation, channel, param1, param2);
			snd_seq_ev_set_controller(&ev, channel, param1, param2);
			break;

		case 0xC0:
			if (!arguments.silent && arguments.verbose)
				printf("Serial  0x%x Program change     %03u %03u\n", operation, channel, param1);
			snd_seq_ev_set_pgmchange(&ev, channel, param1);
			break;

		case 0xD0:
			if (!arguments.silent && arguments.verbose)
				printf("Serial  0x%x Channel change     %03u %03u\n", operation, channel, param1);
			snd_seq_ev_set_chanpress(&ev, channel, param1);
			break;

		case 0xE0:
			param1 = (param1 & 0x7F) + ((param2 & 0x7F) << 7);
			if (!arguments.silent && arguments.verbose)
				printf("Serial  0x%x Pitch bend         %03u %05i\n", operation, channel, param1);
			snd_seq_ev_set_pitchbend(&ev, channel, param1 - 8192); // in alsa MIDI we want signed int
			break;

		/* Not implementing system commands (0xF0) */

		default:
			if (!arguments.silent)
				printf("0x%x Unknown MIDI cmd   %03u %03u %03u\n", operation, channel, param1, param2);
			break;
	}

	snd_seq_event_output_direct(seq, &ev);
	snd_seq_drain_output(seq);
}

void write_midi_action_to_serial_port(snd_seq_t* seq_handle)
{
	snd_seq_event_t* ev;
	char bytes[] = {0x00, 0x00, 0xFF};

	do
	{
		snd_seq_event_input(seq_handle, &ev);

		switch (ev->type)
		{

			case SND_SEQ_EVENT_NOTEOFF:
				bytes[0] = 0x80 + ev->data.control.channel;
				bytes[1] = ev->data.note.note;
				bytes[2] = ev->data.note.velocity;
				if (!arguments.silent && arguments.verbose)
					printf("Alsa    0x%x Note off           %03u %03u %03u\n", bytes[0]&0xF0, bytes[0]&0xF, bytes[1], bytes[2]);
				break;

			case SND_SEQ_EVENT_NOTEON:
				bytes[0] = 0x90 + ev->data.control.channel;
				bytes[1] = ev->data.note.note;
				bytes[2] = ev->data.note.velocity;
				if (!arguments.silent && arguments.verbose)
					printf("Alsa    0x%x Note on            %03u %03u %03u\n", bytes[0]&0xF0, bytes[0]&0xF, bytes[1], bytes[2]);
				break;

			case SND_SEQ_EVENT_KEYPRESS:
				bytes[0] = 0x90 + ev->data.control.channel;
				bytes[1] = ev->data.note.note;
				bytes[2] = ev->data.note.velocity;
				if (!arguments.silent && arguments.verbose)
					printf("Alsa    0x%x Pressure change    %03u %03u %03u\n", bytes[0]&0xF0, bytes[0]&0xF, bytes[1], bytes[2]);
				break;

			case SND_SEQ_EVENT_CONTROLLER:
				bytes[0] = 0xB0 + ev->data.control.channel;
				bytes[1] = ev->data.control.param;
				bytes[2] = ev->data.control.value;
				if (!arguments.silent && arguments.verbose)
					printf("Alsa    0x%x Controller change  %03u %03u %03u\n", bytes[0]&0xF0, bytes[0]&0xF, bytes[1], bytes[2]);
				break;

			case SND_SEQ_EVENT_PGMCHANGE:
				bytes[0] = 0xC0 + ev->data.control.channel;
				bytes[1] = ev->data.control.value;
				if (!arguments.silent && arguments.verbose)
					printf("Alsa    0x%x Program change     %03u %03u %03u\n", bytes[0]&0xF0, bytes[0]&0xF, bytes[1], bytes[2]);
				break;

			case SND_SEQ_EVENT_CHANPRESS:
				bytes[0] = 0xD0 + ev->data.control.channel;
				bytes[1] = ev->data.control.value;
				if (!arguments.silent && arguments.verbose)
					printf("Alsa    0x%x Channel change     %03u %03u %03u\n", bytes[0]&0xF0, bytes[0]&0xF, bytes[1], bytes[2]);
				break;

			case SND_SEQ_EVENT_PITCHBEND:
				bytes[0] = 0xE0 + ev->data.control.channel;
				ev->data.control.value += 8192;
				bytes[1] = (int)ev->data.control.value & 0x7F;
				bytes[2] = (int)ev->data.control.value >> 7;
				if (!arguments.silent && arguments.verbose)
					printf("Alsa    0x%x Pitch bend         %03u %5d\n", bytes[0]&0xF0, bytes[0]&0xF, ev->data.control.value);
				break;

			default:
				break;
		}

		if (bytes[0]!=0x00)
		{
			bytes[1] = (bytes[1] & 0x7F); // just to be sure that one bit is really zero
			if (bytes[2]==0xFF) {
				write(serial, bytes, 2);
			} else {
				bytes[2] = (bytes[2] & 0x7F);
				write(serial, bytes, 3);
			}
		}

		snd_seq_free_event(ev);

	} while (snd_seq_event_input_pending(seq_handle, 0) > 0);
}


void* read_midi_from_alsa(void* seq)
{
	int npfd;
	struct pollfd* pfd;
	snd_seq_t* seq_handle;

	seq_handle = seq;

	npfd = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
	pfd = (struct pollfd*) alloca(npfd * sizeof(struct pollfd));
	snd_seq_poll_descriptors(seq_handle, pfd, npfd, POLLIN);

	while (run)
	{
		if (poll(pfd,npfd, 100) > 0)
		{
			write_midi_action_to_serial_port(seq_handle);
		}
	}

	printf("\nStopping [PC]->[Hardware] communication...");
}
#endif

void* read_midi_from_serial_port(void* ptr)
{
	char buf[4], msg[MAX_MSG_SIZE];
	int msglen;

        jackdata_t* jackdata = (jackdata_t*) ptr;

	/* Lets first fast forward to first status byte... */
	if (!arguments.printonly) {
		do read(serial, buf, 1);
		while (buf[0] >> 7 == 0);
	}

	while (run)
	{
		/*
		 * super-debug mode: only print to screen whatever
		 * comes through the serial port.
		 */

		if (arguments.printonly)
		{
			read(serial, buf, 1);
			printf("%x\t", (int) buf[0]&0xFF);
			fflush(stdout);
			continue;
		}

		/*
		 * so let's align to the beginning of a midi command.
		 */

		int i = 1;

		while (i < 3) {
			read(serial, buf+i, 1);

			if (buf[i] >> 7 != 0) {
				/* Status byte received and will always be first bit!*/
				buf[0] = buf[i];
				i = 1;
			} else {
				/* Data byte received */
				if (i == 2) {
					/* It was 2nd data byte so we have a MIDI event process! */
					i = 3;
				} else {
					/* Lets figure out are we done or should we read one more byte. */
					if ((buf[0] & 0xF0) == 0xC0 || (buf[0] & 0xF0) == 0xD0) {
						i = 3;
					} else {
						i = 2;
					}
				}
			}

		}

		/* print comment message (the ones that start with 0xFF 0x00 0x00 */
		if (buf[0] == (char) 0xFF && buf[1] == (char) 0x00 && buf[2] == (char) 0x00)
		{
			read(serial, buf, 1);
			msglen = buf[0];
			if (msglen > MAX_MSG_SIZE-1) msglen = MAX_MSG_SIZE-1;

			read(serial, msg, msglen);

			if (arguments.silent) continue;

			/* make sure the string ends with a null character */
			msg[msglen] = 0;

			puts("0xFF Non-MIDI message: ");
			puts(msg);
			putchar('\n');
			fflush(stdout);
		}

		/* parse MIDI message */
		else
                {
                    jack_ringbuffer_write(jackdata->ringbuffer, buf, 3);
                }
	}

	return NULL;
}

/* --------------------------------------------------------------------- */
// Main program

int main(int argc, char** argv)
{
	//arguments arguments;
	struct termios oldtio, newtio;
	jackdata_t jackdata;

	arg_set_defaults(&arguments);
	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	/*
	 * Open MIDI output port
	 */

	open_client(&jackdata);

	/*
	 *  Open modem device for reading and not as controlling tty because we don't
	 *  want to get killed if linenoise sends CTRL-C.
	 */

	serial = open(arguments.serialdevice, O_RDWR | O_NOCTTY);

	if (serial < 0)
	{
		perror(arguments.serialdevice);
		exit(-1);
	}

	/* save current serial port settings */
	tcgetattr(serial, &oldtio);

	/* clear struct for new port settings */
	bzero(&newtio, sizeof(newtio));

	/*
	 * BAUDRATE : Set bps rate. You could also use cfsetispeed and cfsetospeed.
	 * CRTSCTS  : output hardware flow control (only used if the cable has
	 * all necessary lines. See sect. 7 of Serial-HOWTO)
	 * CS8      : 8n1 (8bit, no parity, 1 stopbit)
	 * CLOCAL   : local connection, no modem contol
	 * CREAD    : enable receiving characters
	 */
	newtio.c_cflag = arguments.baudrate | CS8 | CLOCAL | CREAD; // CRTSCTS removed

	/*
	 * IGNPAR  : ignore bytes with parity errors
	 * ICRNL   : map CR to NL (otherwise a CR input on the other computer
	 * will not terminate input)
	 * otherwise make device raw (no other input processing)
	 */
	newtio.c_iflag = IGNPAR;

	/* Raw output */
	newtio.c_oflag = 0;

	/*
	 * ICANON  : enable canonical input
	 * disable all echo functionality, and don't send signals to calling program
	 */
	newtio.c_lflag = 0; // non-canonical

	/*
	 * set up: we'll be reading 4 bytes at a time.
	 */
	newtio.c_cc[VTIME]    = 0;     /* inter-character timer unused */
	newtio.c_cc[VMIN]     = 1;     /* blocking read until n character arrives */

	/*
	 * now clean the modem line and activate the settings for the port
	 */
	tcflush(serial, TCIFLUSH);
	tcsetattr(serial, TCSANOW, &newtio);

        // Linux-specific: enable low latency mode (FTDI "nagling off")
        //struct serial_struct ser_info;
        //ioctl(serial, TIOCGSERIAL, &ser_info);
        //ser_info.flags |= ASYNC_LOW_LATENCY;
        //ioctl(serial, TIOCSSERIAL, &ser_info);

	if (arguments.printonly)
	{
		printf("Super debug mode: Only printing the signal to screen. Nothing else.\n");
	}

#if 0
	/*
	 * read commands
	 */

	/* Starting thread that is polling alsa midi in port */
	pthread_t midi_out_thread, midi_in_thread;
	int iret1, iret2;
	iret1 = pthread_create(&midi_out_thread, NULL, read_midi_from_alsa, (void*) seq);
#endif
        run = true;

        /* And also thread for polling serial data. As serial is currently read in
           blocking mode, by this we can enable ctrl+c quiting and avoid zombie
           alsa ports when killing app with ctrl+z */
        pthread_t midi_in_thread;
        pthread_create(&midi_in_thread, NULL, read_midi_from_serial_port, (void*) &jackdata);

	signal(SIGINT, exit_cli);
	signal(SIGTERM, exit_cli);

	while (run)
	{
		usleep(10000);
	}

	close_client(&jackdata);
	pthread_join(midi_in_thread, NULL);

	/* restore the old port settings */
	tcsetattr(serial, TCSANOW, &oldtio);
	printf("\ndone!\n");
}
