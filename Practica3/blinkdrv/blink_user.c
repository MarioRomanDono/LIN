#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define BLINK_UNO "/dev/usb/blinkstick0"
#define BLINK_DOS "/dev/usb/blinkstick1"


int escribirEnBlinkstick(char * dispositivo, char * cadena) {
	int fd = open(dispositivo, O_WRONLY);
	int res;

	if (fd == -1) {
		return -1;
	}
	res = write(fd, cadena, strlen(cadena));
	if (res == -1) {
		return -1;
	}
	res = close(fd);
	if (res == -1) {
		return -1;
	}
	return 0;
}

int secuenciaColores(unsigned int color) {
	char ultimo_uno[90] = "";
	char ultimo_dos[90] = "";
	char cadena[90];
	int limite = 15;
	int led = 0;
	int led_mod = 0;
	int i;

	//Secuencia descendente
	while (limite >= 0) {
		led = 0;
		while (led <= limite) {
			led_mod = led % 8;
			if (sprintf(cadena, "%d:0x%x", led_mod, color) < 0) {
				perror("Error on blink_user");
				return -1;
			}

			if (led < 8) { //Escribimos en el blinkstick 1
				if (strlen(ultimo_uno) > 0) {
					strcat(cadena, ",");
				}
				strcat(cadena, ultimo_uno);
				if (escribirEnBlinkstick(BLINK_UNO, cadena) == -1 || escribirEnBlinkstick(BLINK_DOS, ultimo_dos) == -1) {
					perror("Error on blink_user");
					return -1;
				}			
			}
			else { //Escribimos en el blinkstick 2
				if (strlen(ultimo_dos) > 0) {
					strcat(cadena, ",");
				}
				strcat(cadena, ultimo_dos);
				if (escribirEnBlinkstick(BLINK_UNO, ultimo_uno) == -1 || escribirEnBlinkstick(BLINK_DOS, cadena) == -1) {
					perror("Error on blink_user");
					return -1;
				}
			}
			usleep(50000);
			led++;
		}

		if (led <= 8) {
			strcpy(ultimo_uno, cadena);
		}  else  {
			strcpy(ultimo_dos, cadena);
		}
		limite--;
	}

	//Parpadeo
	for (i = 0; i < 3; i++) {
		if (escribirEnBlinkstick(BLINK_UNO, ultimo_uno) == -1 || escribirEnBlinkstick(BLINK_DOS, ultimo_dos) == -1) {
			perror("Error on blink_user");
			return -1;
		}
		usleep(500000);
		if (escribirEnBlinkstick(BLINK_UNO, "") == -1 || escribirEnBlinkstick(BLINK_DOS, "") == -1) {
			perror("Error on blink_user");
			return -1;
		}
		usleep(500000);
	}

	// Leds fijos
	if (escribirEnBlinkstick(BLINK_UNO, ultimo_uno) == -1 || escribirEnBlinkstick(BLINK_DOS, ultimo_dos) == -1) {
			perror("Error on blink_user");
			return -1;
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