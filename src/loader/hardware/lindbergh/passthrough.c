#include <stdio.h>
#include <stdint.h>

#if defined(__linux__) || defined(__APPLE__)
#include <string.h>    /* String function definitions */
#include <unistd.h>    /* UNIX standard function definitions */
#include <fcntl.h>     /* File control definitions */
#include <errno.h>     /* Error number definitions */
#include <termios.h>   /* POSIX terminal control definitions */
#include <sys/ioctl.h> /* Ioctl function to control device drivers in the kernel */
#endif

#ifdef _WIN32
#include <windows.h> /* Windows API functions and types */
#endif

#include "passthrough.h"

#if defined(__linux__) || defined(__APPLE__)
    int jvsHandle = -1;
#elif defined(_WIN32)
    HANDLE jvsHandle = INVALID_HANDLE_VALUE;
#endif

#define SYNC 0xE0
#define ESCAPE 0xD0

/**
 * Setups the JVS passthrough by opening the specified serial port and configuring its settings.
 * @param jvsPath The path to the serial port device (e.g., "/dev/ttyUSB0").
 * @return JVS_PASSTHROUGH_STATUS_OK on success, JVS_PASSTHROUGH_STATUS_ERROR on failure.
 */
JVSPassthroughStatus initJVSPassthroughLinux(char *jvsPath)
{
#if defined(__linux__) || defined(__APPLE__)
    jvsHandle = open(jvsPath, O_RDWR | O_NOCTTY);
    if (jvsHandle < 0)
    {
        printf("Error: Failed to open '%s' for JVS passthrough\n", jvsPath);
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }

    struct termios options;

    if (tcgetattr(jvsHandle, &options) != 0)
    {
        printf("Error: Failed to get terminal attributes for JVS passthrough\n");
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }

    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag |= CREAD; // Turn on READ, let ctrl lines work

    options.c_cflag &= ~PARENB;  // Clear parity bit & disable parity
    options.c_cflag &= ~CSTOPB;  // Clear stop field, 1 stop bit
    options.c_cflag &= ~CSIZE;   // Clear all bits that set the data size
    options.c_cflag |= CS8;      // 8 bits
    options.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control

    options.c_lflag &= ~ICANON;                 // Disable canonical mode, so no input processing is performed
    options.c_lflag &= ~ECHO;                   // Disable echo
    options.c_lflag &= ~ECHOE;                  // Disable erasure
    options.c_lflag &= ~ECHONL;                 // Disable new-line echo
    options.c_lflag &= ~ISIG;                   // Disable interpretation of INTR, QUIT and SUSP
    options.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    options.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                         ICRNL); // Disable any special handling of received bytes

    options.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    options.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 2; // 200ms

    if (tcsetattr(jvsHandle, TCSANOW, &options) != 0)
    {
        printf("Error: Failed to set terminal attributes for JVS passthrough\n");
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }

#endif

    return JVS_PASSTHROUGH_STATUS_OK;
}

/**
 * Setups the JVS passthrough by opening the specified serial port and configuring its settings.
 * @param jvsPath The path to the serial port device (e.g., "/dev/ttyUSB0").
 * @return JVS_PASSTHROUGH_STATUS_OK on success, JVS_PASSTHROUGH_STATUS_ERROR on failure.
 */
JVSPassthroughStatus initJVSPassthroughWindows(char *jvsPath)
{
#if defined(_WIN32)
    jvsHandle = CreateFileA(jvsPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (jvsHandle == INVALID_HANDLE_VALUE)
    {
        printf("Error: Failed to open '%s' for JVS passthrough\n", jvsPath);
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }

    DCB dcb;
    COMMTIMEOUTS timeouts;

    SecureZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(jvsHandle, &dcb))
    {
        printf("Error: Failed to get comm state for JVS passthrough\n");
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;

    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;

    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(jvsHandle, &dcb))
    {
        printf("Error: Failed to set comm state for JVS passthrough\n");
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }

    SecureZeroMemory(&timeouts, sizeof(timeouts));

    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 50;

    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;

    if (!SetCommTimeouts(jvsHandle, &timeouts))
    {
        printf("Error: Failed to set comm timeouts for JVS passthrough\n");
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }
#endif
    return JVS_PASSTHROUGH_STATUS_OK;
}

/**
 * Setups the JVS passthrough by opening the specified serial port and configuring its settings.
 * @param jvsPath The path to the serial port device (e.g., "/dev/ttyUSB0").
 * @return JVS_PASSTHROUGH_STATUS_OK on success, JVS_PASSTHROUGH_STATUS_ERROR on failure.
 */
JVSPassthroughStatus initJVSPassthrough(char *jvsPath)
{
#if defined(__linux__) || defined(__APPLE__)
    return initJVSPassthroughLinux(jvsPath);
#elif defined(_WIN32)
    return initJVSPassthroughWindows(jvsPath);
#else
    printf("Error: Unsupported platform for JVS passthrough\n");
    return JVS_PASSTHROUGH_STATUS_ERROR;
#endif
}

/**
 * Writes bytes to the serial port.
 * @param buffer The buffer containing the data to write.
 * @param size The size of the data to write.
 * @return The number of bytes written, or 0 on failure.
 */
int writeBytes(const unsigned char *buffer, int size)
{
#if defined(__linux__) || defined(__APPLE__)
    return write(jvsHandle, buffer, size);
#elif defined(_WIN32)
    DWORD written = 0;

    if (!WriteFile(jvsHandle, buffer, size, &written, NULL)) {
        printf("Error: Failed to write to JVS passthrough\n");
        return 0;
    }

    return (int) written;
#else
    printf("Error: Cannot write, unsupported platform for JVS passthrough\n");
    return JVS_PASSTHROUGH_STATUS_ERROR;
#endif
}

/**
 * Reads bytes from the serial port.
 * @param buffer The buffer to store the received data.
 * @param size The size of the buffer.
 * @return The number of bytes read, or 0 on failure.
 */
int readBytes(unsigned char *buffer, int size)
{
#if defined(__linux__) || defined(__APPLE__)
    return read(jvsHandle, buffer, size);
#elif defined(_WIN32)
    DWORD read = 0;

    if (!ReadFile(jvsHandle, buffer, size, &read, NULL)) {
        printf("Error: Failed to read from JVS passthrough\n");
        return 0;
    }

    return (int) read;
#else
    printf("Error: Cannot read, unsupported platform for JVS passthrough\n");
    return JVS_PASSTHROUGH_STATUS_ERROR;
#endif
}

/**
 * Writes a JVS frame to the serial port.
 * @param buffer The buffer containing the JVS frame data to write.
 * @param size The size of the data to write.
 * @return JVS_PASSTHROUGH_STATUS_OK on success, JVS_PASSTHROUGH_STATUS_ERROR on failure.
 */
JVSPassthroughStatus writeJVSFrame(unsigned char *buffer, int size)
{

    int written = writeBytes(buffer, size);
    if (written < 0)
    {
        printf("Error: Failed to write to JVS passthrough\n");
        return JVS_PASSTHROUGH_STATUS_ERROR;
    }

    return JVS_PASSTHROUGH_STATUS_OK;
}

/**
 * Reads a JVS frame from the serial port.
 * @param buffer The buffer to store the received JVS frame data.
 * @param size A pointer to an integer containing the size of the buffer. On return, this will be updated with the number of bytes read.
 * @return JVS_PASSTHROUGH_STATUS_OK on success, JVS_PASSTHROUGH_STATUS_ERROR on failure.
 */
JVSPassthroughStatus readJVSFrame(unsigned char *buffer, int *size)
{
    int bytesAvailable = 0, escape = 0, phase = 0, index = 0, finished = 0;
	unsigned char checksum = 0x00;
    int timeout = 3;

    unsigned char destination, length;
    unsigned char inputBuffer[JVSBUFFER_SIZE];

    *size = 0;

	while (!finished)
	{
		int bytesRead = read(jvsHandle, inputBuffer + bytesAvailable, JVSBUFFER_SIZE - bytesAvailable);

        if(bytesRead > 0) {
            timeout = 3;
        } else {
            timeout = timeout - 1;
        }

        if(timeout == 0) {
            return JVS_PASSTHROUGH_STATUS_TIMEOUT;
        }


		if (bytesRead < 0)
			return JVS_PASSTHROUGH_STATUS_ERROR;

		bytesAvailable += bytesRead;

		while ((index < bytesAvailable) && !finished)
		{
			/* If we encounter a SYNC start again */
			if (!escape && (inputBuffer[index] == SYNC))
			{
				phase = 0;
				*size = 0;
				buffer[*size++] = inputBuffer[index];
				index++;
				continue;
			}

			/* If we encounter an ESCAPE byte escape the next byte */
			if (!escape && inputBuffer[index] == ESCAPE)
			{
				escape = 1;
				index++;
				continue;
			}

			/* Escape next byte by adding 1 to it */
			if (escape)
			{
				inputBuffer[index]++;
				escape = 0;
			}

			/* Deal with the main bulk of the data */
			switch (phase)
			{
			case 0: // If we have not yet got the address
				buffer[*size++] = inputBuffer[index];
				destination = inputBuffer[index];
				checksum = destination & 0xFF;
				phase++;
				break;
			case 1: // If we have not yet got the length
				length = inputBuffer[index];
				buffer[*size++] = inputBuffer[index];
				checksum = (checksum + length) & 0xFF;
				phase++;
				break;
			case 2: // If there is still data to read
				if (*size == (length + 2))
				{
					if (checksum != inputBuffer[index]) {
                        printf("CHEKCSUM ERROR\n");
                        return JVS_PASSTHROUGH_STATUS_ERROR; // Checksum error, return empty frame
                    }

				    buffer[*size++] = checksum;

                    
					finished = 1;
					break;
				}
				buffer[*size++] = inputBuffer[index];
				checksum = (checksum + inputBuffer[index]) & 0xFF;
				break;
			default:
				return JVS_PASSTHROUGH_STATUS_ERROR;
			}
			index++;
		}
	}

    return JVS_PASSTHROUGH_STATUS_OK;
}