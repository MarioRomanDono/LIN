#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define BLINK_UNO "/dev/usb/blinkstick0"
#define BLINK_DOS "/dev/usb/blinkstick1"


int escribirEnBlinkstick(char * dispositivo, char * cadena) {
	int fd = open(dispositivo, O_WRONLY);
	if (fd == -1) {
		return -1;
	}
	return write(fd, cadena, strlen(cadena));
}

int error() {
	fprintf(stderr, "Error\n");
	return -1;
}

int secuenciaColores(unsigned int color) {
	char ultimo_uno[90] = "";
	char ultimo_dos[90] = "";
	char cadena[90];
	int limite = 15;
	int led = 0;
	int led_mod = 0;

	while (limite >= 0) {
		while (led <= limite) {
			led_mod = led % 8;
			if (sprintf(cadena, "%d:0x%x,", led_mod, color) < 0) {
				return error();
			}

			if (led < 8) { //Escribimos en el blinkstick 1
				strcat(cadena, ultimo_uno);
				if (escribirEnBlinkstick(BLINK_UNO, cadena) == -1 || escribirEnBlinkstick(BLINK_DOS, ultimo_dos) == -1) {
					return error();
				}			
			}
			else { //Escribimos en el blinkstick 2
				strcat(cadena, ultimo_dos);
				if (escribirEnBlinkstick(BLINK_UNO, ultimo_uno) == -1 || escribirEnBlinkstick(BLINK_DOS, cadena) == -1) {
					return error();
				}
			}
			sleep(0.5);
			led++;
		}

		if (led < 8) {
			strcpy(ultimo_uno, cadena);
		}  else  {
			strcpy(ultimo_dos, cadena);
		}
		limite--;
	}
	return 0;
}

int main(int argc, char ** argv) {
	unsigned int color;
	int res;

	if (argc != 2) {
		fprintf(stderr, "Usage: ./blink_user <color>\n");
		return -1;
	}

	color = strtoul(argv[1], NULL, 16);

	res = secuenciaColores(color);

	return res;
}