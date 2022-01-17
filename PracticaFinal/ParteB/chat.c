#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHARS_MSG 128

typedef enum {
    NORMAL_MSG,
    USERNAME_MSG,
    END_MSG
} message_type_t;

struct chat_message {
    char contenido[MAX_CHARS_MSG];
    message_type_t type;
};

struct thread_args {
    char * username;
    char * path_fifo;
};

void * send_thread(void * arguments) {
    int fd_fifo, wbytes, bytes;
    struct thread_args * args = (struct thread_args * ) arguments;
    struct chat_message message;
    int size = sizeof(struct chat_message);

    // 1. Abre FIFO de envío en modo escritura

    fd_fifo=open(args->path_fifo,O_WRONLY);

    if (fd_fifo<0) {
        perror(args->path_fifo);
        exit(1);
    }

    printf("Conexión de envío establecida!!\n");

    // 2. Enviar nombre de usuario
    strncpy(message.contenido, args->username, MAX_CHARS_MSG);
    message.type = USERNAME_MSG;

    wbytes = write(fd_fifo, &message, size);
    if (wbytes > 0 && wbytes!=size) {
        fprintf(stderr,"Can't write the whole register\n");
        exit(1);
    } else if (wbytes < 0){
        perror("Error when writing to the FIFO");
        exit(1);
    } 

    // 3. Enviar mensajes
    message.type = NORMAL_MSG;
    while((bytes=read(0,message.contenido,MAX_CHARS_MSG))>0) {
        message.contenido[bytes] = '\0';
        wbytes=write(fd_fifo,&message,size);
        if (wbytes > 0 && wbytes!=size) {
            fprintf(stderr,"Can't write the whole register\n");
            exit(1);
        } else if (wbytes < 0){
            perror("Error when writing to the FIFO");
            exit(1);
        }       
    }

    if (bytes < 0) {
        fprintf(stderr,"Error when reading from stdin\n");
        exit(1);
    } 

    // 4. Enviar fin de comunicación, cerrar FIFO y salir
    message.type = END_MSG;
    wbytes=write(fd_fifo,&message,size);
    if (wbytes > 0 && wbytes!=size) {
        fprintf(stderr,"Can't write the whole register\n");
        exit(1);
    } else if (wbytes < 0){
        perror("Error when writing to the FIFO");
        exit(1);
    }         

    close(fd_fifo);

    exit(0);  
}

void * recv_thread(void * arg) {
    char * path_fifo = (char *) arg;
    int fd_fifo, bytes;
    struct chat_message message;
    int size = sizeof(struct chat_message);
    char username[MAX_CHARS_MSG];

    // 1. Abre FIFO de recepcion en modo lectura

    fd_fifo=open(path_fifo,O_RDONLY);

    if (fd_fifo<0) {
        perror(path_fifo);
        exit(1);
    }

    printf("Conexión de recepción establecida!!\n");

    // 2. Procesar nombre de usuario 

    if ((bytes=read(fd_fifo,&message,size)) == size) {
        if (message.type != USERNAME_MSG) {
            fprintf(stderr,"Error while receiving username\n");
            exit(1);
        }

        strncpy(username, message.contenido, MAX_CHARS_MSG);
    }
    else if (bytes > 0){
        fprintf(stderr,"Can't read the whole register\n");
        exit(1);
    } else if (bytes < 0) {
        perror("Error when reading from the FIFO\n");
        exit(1);
    }

    // 3. Procesar el resto de mensajes

    while((bytes=read(fd_fifo,&message,size))==size) {
        if (message.type == END_MSG) 
            break;

        printf("%s dice: %s\n", username, message.contenido);

        /* if (wbytes!=strlen(message.contenido)) {
            fprintf(stderr,"Can't write data to stdout, size:%d %d\n", wbytes, strlen(message.contenido));
            exit(1);
        }  */

    }
    if (bytes > 0 && bytes != size){
        fprintf(stderr,"Can't read the whole register\n");
        exit(1);
    } else if (bytes < 0) {
        perror("Error when reading from the FIFO\n");
        exit(1);
    }

    // 4. Cerrar FIFO de recepción y salir 

    printf("Conexión finalizada por %s!!\n", username); 

    close(fd_fifo);

    exit(0);
}

int main(int argc, char ** argv) {
    pthread_t send, recv;
    struct thread_args args;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <nombre> <ruta-fifo1> <ruta-fifo2>\n", argv[0]);
        exit(1);
    }

    args.username = argv[1];
    args.path_fifo = argv[2];

    // Create threads
    pthread_create(&send, NULL, send_thread, (void *) &args);
    pthread_create(&recv, NULL, recv_thread, (void *) argv[3]);

    // Wait until ending
    pthread_join(send, NULL);
    pthread_join(recv, NULL);

    return 0;
}