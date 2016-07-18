#ifndef TFTP_H
#define TFTP_H

#include <arpa/inet.h>  //funciones usadas para internet
#include <errno.h>
#include <fcntl.h>   //constantes tipo O_*
#include <signal.h>  //señales
#include <stdarg.h>  //Manejar argumentos del tipo " ... "
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>  //socket
#include <sys/stat.h>    //información sobre atributos de archivos
#include <sys/time.h>    //funciones de tiempo
#include <sys/types.h>   //tipos de dato *_t para el Sistema Operativo
#include <sys/wait.h>    //
#include <syslog.h>      //log del sistema
#include <unistd.h>      //llamadas al sistema

#define OPCODE_RRQ 1
#define OPCODE_WRQ 2
#define OPCODE_DATA 3
#define OPCODE_ACK 4
#define OPCODE_ERROR 5

#define DEF_RETRIES 20
#define DEF_TIMEOUT_SEC 1
#define DEF_TIMEOUT_USEC 500
#define BUFSIZE 512
#define MAX_BUFSIZE ( 4 + BUFSIZE )
#define ACK_BUFSIZE 4
#define NAMESIZE 255

#define MODE_OCTET "octet"
#define MODE_NETASCII "netascii"

#define ERR_NOT_DEFINED 0
#define ERR_NOT_FOUND 1
#define ERR_ACCESS_DENIED 2
#define ERR_DISK_FULL 3
#define ERR_ILLEGAL_OP 4
#define ERR_UNKNOWN_TID 5
#define ERR_FILE_EXISTS 6
#define ERR_NO_SUCH_USER 7

#define STATE_STANDBY 0
#define STATE_DATA_SENT 1
#define STATE_ACK_SENT 2

#define SENT_READY 0
#define SENT_SENDING 1

#define ACK_READY 0
#define ACK_SENDING 1

#define DEFAULT_SERVER_PORT 69

typedef struct tftp {
    int                local_descriptor; /* descriptor de socket local */
    int                fd;               /* descriptor de archivo */
    int                retries;          /* reintentos */
    uint16_t           state;            /* estado */
    uint16_t           tid;              /* id de transferencia */
    uint16_t           err;              /* tipo de error */
    int32_t            blknum;           /* numero de bloque */
    char *             msgerr;           /*  msg de error  */
    char *             mode;             /* modo de transferencia */
    char               file[NAMESIZE];   /* nombre del archivo */
    struct sockaddr_in remote_addr;      /* estructura remota */
    struct sockaddr_in local_addr;       /* estructura local */
    struct timeval     timeout;          /* tiempo de espera para cada msg */
    socklen_t          size_remote;      /* tamaño estructura remota */
    socklen_t          size_local;       /* tamaño estructura local */
    u_char             msg[BUFSIZE];     /* tamaño max msg a enviar */
    u_char             buf[MAX_BUFSIZE]; /* tamaño max msg a recibir */

} tftp_t;

typedef struct tftp_listen {
    int                descriptor;       /* descriptor de socket escucha */
    uint16_t           state;            /* estado */
    char *             mode;             /* modo de transferencia */
    struct sockaddr_in addr;             /* estructura escucha */
    struct sockaddr_in remote_addr;      /* estructura remota */
    socklen_t          size;             /* tamaño estructura escucha */
    socklen_t          remote_size;      /* tamaño estructura escucha */
    u_char             buf[MAX_BUFSIZE]; /* tamaño max msg a recibir */

} tftp_tl;

void err_log_exit ( int priority, const char *format, ... );

void _err_log_exit ( int priority, const char *format, ... );

void build_data_msg ( tftp_t *instance );

void build_error ( tftp_t *instance );

void build_ack_msg ( tftp_t *instance );

void dec_data ( tftp_t *instance );

void data_send ( tftp_t *instance );

void ack_send ( tftp_t *instance );

#endif
