#include <ctype.h>  //Declares several functions that are useful for testing and mapping characters.
#include "tftp.h"

#define CLIENT_NAME "client"

/* _exit_free
   Libera los punteros enviados y se sale del programa

   type: tipo de salida, EXIT_SUCCESS o EXIT_FAILURE
   params: cantidad de parámetros
   ... : los parámetros en si
*/

void _exit_free ( int type, int params, ... ) {
    va_list p_list;
    int     i = 0;

    va_start ( p_list, params );

    for ( ; i < params; i++ )
        free ( va_arg ( p_list, void * ) );

    va_end ( p_list );

    _exit ( type );
}

void build_request ( tftp_t *instance, int type ) {
    u_char *p;
    memset ( instance->buf, 0, MAX_BUFSIZE );

    p = instance->buf;

    if ( type == OPCODE_RRQ ) {
        *p = ( OPCODE_RRQ >> 8 ) & 0xff;
        p++;
        *p = OPCODE_RRQ & 0xff;
        p++;

    } else if ( type == OPCODE_WRQ ) {
        *p = ( OPCODE_WRQ >> 8 ) & 0xff;
        p++;
        *p = OPCODE_WRQ & 0xff;
        p++;

    } else {
        puts ( "Unknown type request" );
        _exit ( EXIT_FAILURE );
    }

    memcpy ( p, instance->file, strlen ( instance->file ) );
    p += strlen ( instance->file ) + 1;
    memcpy ( p, instance->mode, strlen ( instance->mode ) );
}

void data_send_cli ( tftp_t *instance ) {
    ssize_t     sent, received;
    off_t       offset;
    static bool timeout = false;

    // Primero se espera el ACK del servidor y luego se responde un DATA

    received = recvfrom (
        instance->local_descriptor, instance->buf, MAX_BUFSIZE, 0,
        ( struct sockaddr * ) &instance->remote_addr, &instance->size_remote );

    /*
      Verificamos que haya llegado un msg válido, se debe cumplir:
      1. Que received sea distinto a -1
      2. Que el OPCODE sea OPCODE_ACK
      3. Que el ack corresponda al blknum que esperamos
      4. Que no haya expirado el socket
    */

    if ( received != -1
         && ( ( instance->buf[0] << 8 ) + instance->buf[1] == OPCODE_ACK )
         && ( ( instance->buf[2] << 8 ) + instance->buf[3] == instance->blknum )
         && received != EWOULDBLOCK
         /*&& ( instance->tid == ntohs( instance->remote_addr.sin_port ) )*/ ) {
        /* Si hemos enviado el último msg y recibido el último ack, terminamos
         */

        if ( offset < BUFSIZE ) {
            printf ( "File %s sent successful\n", instance->file );

            /* Cerramos el descriptor de archivo y de socket */

            close ( instance->fd );
            close ( instance->local_descriptor );

            /* Hijo finaliza */

            _exit_free ( EXIT_SUCCESS, 1, instance );
        }

        instance->retries = 0;

        /* Limpiamos los buffers */

        memset ( instance->msg, 0, BUFSIZE );
        memset ( instance->buf, 0, MAX_BUFSIZE );

        /* Aumentamos blknum */

        instance->blknum++;

        /* Si el blknum es 65536, le asignamos el valor 0 para no salirnos del
           rango
           de 2 bytes de la trama para el campo blknum */

        if ( instance->blknum == 65536 )
            instance->blknum = 0;

    } else {
        timeout = true;
        instance->retries++;

        if ( instance->retries == DEF_RETRIES ) {
            puts ( "Retries limit reached." );
            _exit ( EXIT_FAILURE );
        }

        printf ( "Retrie number %d in data_send_client(); blknum %d\n",
                 instance->retries, instance->blknum );
    }

    if ( timeout ) {
        /* Regresamos 512 bytes por tener que reenviar el último msg */

        lseek ( instance->fd, -BUFSIZE, SEEK_CUR );

        /* Leemos 512 bytes del archivo */

        offset = read ( instance->fd, instance->msg, BUFSIZE );

        timeout = false;
    } else
        offset = read ( instance->fd, instance->msg, BUFSIZE );

    /* Generamos el msg a enviar */

    build_data_msg ( instance );

    /* Enviamos el msg */

    sent = sendto (
        instance->local_descriptor, instance->buf, ACK_BUFSIZE + offset, 0,
        ( struct sockaddr * ) &instance->remote_addr, instance->size_remote );

    /* Verificamos que se haya enviado correctamente */

    if ( sent != ACK_BUFSIZE + offset )
        printf ( "Error from sendto() in data_send(): %s\n",
                 strerror ( errno ) );
}

void start_wrq ( tftp_t *instance ) {
    ssize_t sent, received;

    build_request ( instance, OPCODE_WRQ );

    sent = sendto ( instance->local_descriptor, instance->buf, MAX_BUFSIZE, 0,
                    ( struct sockaddr * ) &instance->remote_addr,
                    instance->size_remote );

    if ( sent != MAX_BUFSIZE ) {
        printf ( "ERROR Sending write request %s\n", strerror ( errno ) );
        close ( instance->local_descriptor );
        _exit_free ( EXIT_FAILURE, 1, instance );
    }

    /* Iniciamos el temporizador */

    instance->timeout.tv_sec  = DEF_TIMEOUT_SEC;
    instance->timeout.tv_usec = DEF_TIMEOUT_USEC;

    /* Inicializamos las variables a usar */

    instance->blknum = 0;
    instance->fd     = open ( instance->file, O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRWXU | S_IRWXG | S_IRWXO );

    if ( setsockopt ( instance->local_descriptor, SOL_SOCKET, SO_RCVTIMEO,
                      ( char * ) &instance->timeout,
                      sizeof ( instance->timeout ) )
         < 0 ) {
        printf ( "ERROR Sending write request (setsockopt) %s \n",
                 strerror ( errno ) );

        /* Cerramos el descriptor de archivo y de socket */

        close ( instance->fd );
        close ( instance->local_descriptor );

        _exit_free ( EXIT_FAILURE, 1, instance );
    }

    /* Seguimos */
    for ( ;; )
        data_send_cli ( instance );
}

void ack_send_cli ( tftp_t *instance ) {
    ssize_t sent;
    ssize_t received;
    off_t   offset;

    /* Asignamos -1 a blknum en caso de ser 65535 para evitar un rango
     * incorrecto en los ack */

    if ( instance->blknum == 65535 )
        instance->blknum = 0;

    /* Esperamos el siguiente msg */

    received = recvfrom (
        instance->local_descriptor, instance->buf, MAX_BUFSIZE, 0,
        ( struct sockaddr * ) &instance->remote_addr, &instance->size_remote );

    /* Verificamos que haya llegado un msg válido, se debe cumplir: */
    /* 1. Que received sea distinto a -1 */
    /* 2. Que received sea distinto a  EWOULDBLOCK*/
    /* 3. Que el OPCODE sea OPCODE_DATA */
    /* 4. Que el blknum sea el que esperamos */

    if ( received != -1 && received != EWOULDBLOCK
         && ( ( instance->buf[0] << 8 ) + instance->buf[1] == OPCODE_DATA )
         && ( ( instance->buf[2] << 8 ) + instance->buf[3]
              == instance->blknum + 1 ) ) {
        instance->retries = 0;

        /* Procesamos los datos recibidos */
        dec_data ( instance );

        /* Verificamos si es el último msg por recibir */
        if ( received < MAX_BUFSIZE ) {
            /* Escribimos en el archivo */

            write ( instance->fd, instance->msg, received - ACK_BUFSIZE );

            /* Generamos el último ack */

            instance->blknum++;
            build_ack_msg ( instance );

            /* Enviamos el último msg */
            sent = sendto ( instance->local_descriptor, instance->buf,
                            ACK_BUFSIZE, 0,
                            ( struct sockaddr * ) &instance->remote_addr,
                            instance->size_remote );

            /* Verificamos que se haya enviado correctamente */
            if ( sent != ACK_BUFSIZE ) {
                printf ( "Error from sendto() in ack_send(): %s\n",
                         strerror ( errno ) );
                _exit ( EXIT_FAILURE );
            }

            /* Cerramos el descriptor de archivo y de socket */

            close ( instance->fd );
            close ( instance->local_descriptor );

            printf ( "Transfer successful: %s\n", instance->file );

            /* Hijo finaliza */

            _exit ( EXIT_SUCCESS );
        }

        /* Escribimos en el archivo */
        offset = write ( instance->fd, instance->msg, received - ACK_BUFSIZE );

        /* Limpiamos buffer y msg */
        memset ( instance->buf, 0, MAX_BUFSIZE );
        memset ( instance->msg, 0, BUFSIZE );

        /* Incrementamos blknum */
        instance->blknum++;

    } else {  // end 4-condition if

        instance->retries++;

        if ( instance->retries == DEF_RETRIES ) {
            puts ( "Retries limit reached." );
            _exit ( EXIT_FAILURE );
        }

        printf ( "Retrie number %d in ack_send(); blknum %d \n",
                 instance->retries, instance->blknum );
    }

    /* Generamos el ack */
    build_ack_msg ( instance );

    /* Enviamos el ack */
    sent = sendto ( instance->local_descriptor, instance->buf, ACK_BUFSIZE, 0,
                    ( struct sockaddr * ) &instance->remote_addr,
                    instance->size_remote );

    /* Verificamos que se haya enviado correctamente */

    if ( sent != ACK_BUFSIZE ) {
        printf ( "Error from sendto() in ack_send(): %s\n",
                 strerror ( errno ) );
        _exit ( EXIT_FAILURE );
    }
}

void start_rrq ( tftp_t *instance ) {
    ssize_t sent, received;

    /* Comprobamos si hay errores */

    if ( access ( ".", W_OK ) != 0 ) {
        printf ( "ERROR There are no permissions to write.\n" );
        _exit ( EXIT_FAILURE );
    }

    build_request ( instance, OPCODE_RRQ );

    // Enviamos el RRQ

    sent = sendto ( instance->local_descriptor, instance->buf, MAX_BUFSIZE, 0,
                    ( struct sockaddr * ) &instance->remote_addr,
                    instance->size_remote );

    if ( sent != MAX_BUFSIZE ) {
        printf ( "ERROR Sending write request %s\n", strerror ( errno ) );
        _exit ( EXIT_FAILURE );
    }

    /* Asignamos el temporizador para que el socket envía señal cada vez que
     * llega (o no) algo */

    if ( setsockopt ( instance->local_descriptor, SOL_SOCKET, SO_RCVTIMEO,
                      ( char * ) &instance->timeout,
                      sizeof ( instance->timeout ) )
         < 0 ) {
        printf ( "ERROR Sending read request (setsockopt) %s \n",
                 strerror ( errno ) );
        _exit ( EXIT_FAILURE );
    }

    /* Inicializamos las variables a usar */

    instance->blknum = 0;
    instance->fd     = open ( instance->file, O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRWXU | S_IRWXG | S_IRWXO );

    /* Seguimos */
    for ( ;; )
        ack_send_cli ( instance );
}

void start_protocol ( char *ip, int type, char *file ) {
    tftp_t *instance;
    instance = malloc ( sizeof ( struct tftp ) );

    // Inicializaciones generales

    if ( !instance ) {
        printf ( "ERROR Creating Client instance\n", strerror ( errno ) );
        _exit ( EXIT_FAILURE );
    }

    instance->mode        = MODE_OCTET;
    instance->size_remote = sizeof ( struct sockaddr_in );
    instance->size_local  = sizeof ( struct sockaddr_in );
    instance->file        = file;

    // Costruimos socket del servidor

    memset ( &instance->remote_addr, 0, sizeof ( struct sockaddr_in ) );

    if ( inet_pton ( AF_INET, ip, &instance->remote_addr ) != 1 ) {
        printf ( "ERROR Parsing IPv4 server address %s\n", strerror ( errno ) );
        _exit ( EXIT_FAILURE );
    }

    instance->remote_addr.sin_family = AF_INET;
    instance->remote_addr.sin_port   = htons ( DEFAULT_SERVER_PORT );

    // Costruimos socket del cliente

    memset ( &instance->local_addr, 0, sizeof ( struct sockaddr_in ) );
    instance->local_descriptor = socket ( AF_INET, SOCK_DGRAM, 0 );

    if ( instance->local_descriptor == -1 ) {
        printf ( "ERROR Creating Client socket %s\n", strerror ( errno ) );
        _exit ( EXIT_FAILURE );
    }

    instance->local_addr.sin_family      = AF_INET;
    instance->local_addr.sin_addr.s_addr = INADDR_ANY;
    // instance->local_addr.sin_port = htons(0);

    if ( bind ( instance->local_descriptor,
                ( struct sockaddr * ) &instance->local_addr,
                sizeof ( struct sockaddr_in ) )
         == -1 )
        printf ( "ERROR Binding Client socket %s\n", strerror ( errno ) );

    /* Se ejecuta la peticion dependiendo del tipo que sea */

    if ( OPCODE_RRQ == type )
        start_rrq ( instance );
    else
        start_wrq ( instance );

    free ( instance );
}

int main ( int argc, char **argv ) {
    int address_flag = 0, get_flag = 0, put_flag = 0, index = 0, c,
        are_there_errors = 0;
    char *address_value = NULL, *get_value = NULL, *put_value = NULL;

    opterr = 0;

    /*
      El while itera cada argumento y verifica que sean parametros aceptados, la
      cadena "a:g:p:"
      pasada a la función getopt significa que los parámetros -a, -b y -p
      requien el paso de un valor.
    */

    while ( ( c = getopt ( argc, argv, "a:g:p:" ) ) != -1 )
        switch ( c ) {
            case 'a':
                address_flag  = 1;
                address_value = optarg;
                break;

            case 'g':
                get_flag  = 1;
                get_value = optarg;
                break;

            case 'p':
                put_flag  = 1;
                put_value = optarg;
                break;

            case '?':
                are_there_errors = 1;

                if ( optopt == 'a' || optopt == 'g' || optopt == 'p' )
                    fprintf ( stderr, "Option -%c requires an argument.\n",
                              optopt );
                else if ( isprint ( optopt ) )  // isprint() Verifica si un
                                                // caracter es imprimible
                    fprintf ( stderr, "Unknown option `-%c'.\n", optopt );
                else
                    fprintf ( stderr, "Unknown option character `\\x%x'.\n",
                              optopt );
                return EXIT_FAILURE;

            default:
                are_there_errors = 1;
                abort ();
        }

    for ( index = optind; index < argc; index++ ) {
        printf ( "Non-option argument %s\n", argv[index] );
        are_there_errors = 1;
    }

    // Si la sintaxis es correcta, se verifica que los argumentos tengan sentido

    if ( !are_there_errors ) {
        if ( !address_flag ) {
            are_there_errors = 1;
            puts ( "You must specify a server IP address." );
        }

        if ( get_flag && put_flag ) {
            are_there_errors = 1;
            puts ( "You can only specify -g OR -p at time." );
        }

        if ( !( put_flag || get_flag ) ) {
            are_there_errors = 1;
            puts ( "You must specify some file to send or to receive." );
        }
    }

    if ( are_there_errors ) {
        printf (
            "USAGE: %s -a  $(IP_ADDRESS) { -g | -p } $(FILE_NAME)\n"
            "-g get a file from server\n"
            "-p put a file to server\n",
            CLIENT_NAME );

        _exit_free ( EXIT_FAILURE, 3, address_value, get_value, put_value );
    }

    // En caso de un put, verificar que el archivo pueda leerse correctamente

    if ( put_flag ) {
        if ( access ( put_value, F_OK ) != 0 ) {
            puts ( "File not found." );
            _exit_free ( EXIT_FAILURE, 3, address_value, get_value, put_value );
        } else if ( access ( put_value, R_OK ) != 0 ) {
            puts ( "No read permission." );
            _exit_free ( EXIT_FAILURE, 3, address_value, get_value, put_value );
        }
    }

    if ( get_flag )
        start_protocol ( address_value, OPCODE_RRQ, get_value );
    else
        start_protocol ( address_value, OPCODE_WRQ, put_value );

    free ( address_value );
    free ( get_value );
    free ( put_value );

    return EXIT_SUCCESS;
}

/*
  COMO USARLO:  aún tengo que agregar esto a la opción --help o algo asi ...
  $(exe) -a  $(IP_DIR) { -g | -p } $(FILE_NAME)
*/
