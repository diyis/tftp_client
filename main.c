#include <ctype.h>
#include "tftp.h"
#include "cmdline.h"

#define CLIENT_NAME "client"

/*  _exit_free
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

    /* Esperamos el siguiente ack */

    received = recvfrom (
        instance->local_descriptor, instance->buf, MAX_BUFSIZE, 0,
        ( struct sockaddr * ) &instance->remote_addr, &instance->size_remote );

    /*  Verificamos que haya llegado un msg válido, se debe cumplir:
        1. Que received sea distinto a -1
        2. Que received sea distinto a EWOULDBLOCK
        3. Que el OPCODE sea OPCODE_ACK
        4. Que el ack corresponda al blknum que esperamos
        5. Que el msg sea de donde lo esperamos (mismo tid del inicio de la
        transferencia) */

    if ( received != -1 && received != EWOULDBLOCK
         && ( ( instance->buf[0] << 8 ) + instance->buf[1] == OPCODE_ACK )
         && ( ( instance->buf[2] << 8 ) + instance->buf[3] == instance->blknum )
         && ( instance->tid == ntohs ( instance->remote_addr.sin_port ) ) ) {
        instance->retries = 0;

        /*  Si hemos enviado el último msg y recibido el último ack, terminamos  */

        if ( offset < BUFSIZE ) {
            syslog ( LOG_NOTICE, "File %s sent successfully", instance->file );

            /* Cerramos el descriptor de archivo y de socket */

            close ( instance->fd );
            close ( instance->local_descriptor );

            /* Hijo finaliza */

            _exit ( EXIT_SUCCESS );
        }

        /* Limpiamos los buffers */

        memset ( instance->msg, 0, BUFSIZE );
        memset ( instance->buf, 0, MAX_BUFSIZE );

        /* Aumentamos blknum */

        instance->blknum++;

        /*  Si el blknum es 65536, le asignamos el valor 0 para no salirnos del
            rango
            de 2 bytes de la trama para el campo blknum */

        if ( instance->blknum == 65536 )
            instance->blknum = 0;

        return;

    }  // end 4-condition if

    /*  Como no hemos recibido el ack correspondiente a la última trama que
       hemos
        enviado, ha expirado el tiempo de espera */

    timeout = true;
    instance->retries++;
}

void start_wrq ( tftp_t *instance ) {
    ssize_t sent, received;

    build_request ( instance, OPCODE_WRQ );

    sent = sendto ( instance->local_descriptor, instance->buf, 4 + strlen(instance->file) + strlen(instance->mode), 0,
                    ( struct sockaddr * ) &instance->remote_addr,
                    instance->size_remote );

    if ( sent != 4 + strlen(instance->file) + strlen(instance->mode) ) {
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

    /*  Asignamos -1 a blknum en caso de ser 65535 para evitar un rango
        incorrecto
        en los ack */

    if ( instance->blknum == 65535 )
        instance->blknum = -1;

    /* Esperamos el siguiente msg */

    received = recvfrom (
        instance->local_descriptor, instance->buf, MAX_BUFSIZE, 0,
        ( struct sockaddr * ) &instance->remote_addr, &instance->size_remote );

    /* Verificamos que haya llegado un msg válido, se debe cumplir: */
    /* 1. Que received sea distinto a -1 */
    /* 2. Que received sea distinto a EWOULDBLOCK */
    /* 3. Que el OPCODE sea OPCODE_DATA */
    /* 4. Que el blknum sea el que esperamos */
    /*  5. Que el msg sea de donde lo esperamos (mismo tid del inicio de la
        transferencia) */

    if ( received != -1 && received != EWOULDBLOCK
         && ( ( instance->buf[0] << 8 ) + instance->buf[1] == OPCODE_DATA )
         && ( ( instance->buf[2] << 8 ) + instance->buf[3]
              == instance->blknum + 1 )
         && instance->tid == ntohs ( instance->remote_addr.sin_port ) ) {
        /*  Llegando un msg válido, reiniciamos a cero el número máximo de
            reintentos
            permitidos */

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

            if ( sent != ACK_BUFSIZE )
                _err_log_exit ( LOG_ERR,
                                "Error from sendto() in ack_send(): %s",
                                strerror ( errno ) );

            /* Cerramos el descriptor de archivo y de socket */

            close ( instance->fd );
            close ( instance->local_descriptor );

            syslog ( LOG_NOTICE, "File %s received successfully",
                     instance->file );

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
        return;

    }  // end 4-condition if

    instance->retries++;

    if ( instance->retries == DEF_RETRIES )
        _err_log_exit ( LOG_ERR, "Retries limit reached." );

    syslog ( LOG_NOTICE, "Retry number %d in ack_send(); blknum %d",
             instance->retries, instance->blknum + 1 );
}

void start_rrq ( tftp_t *instance ) {
    ssize_t sent, received;

    /* Comprobamos si hay errores */

    if ( access ( ".", W_OK ) != 0 ) {
        printf ( "ERROR There are no permissions to write.\n" );
        _exit ( EXIT_FAILURE );
    }

    build_request ( instance, OPCODE_RRQ );


    if ( sent != 4 + strlen(instance->file) + strlen(instance->mode) ) {
        printf ( "ERROR Sending RRQ %s\n", strerror ( errno ) );
        _exit ( EXIT_FAILURE );
    }

    /*  Asignamos el temporizador para que el socket envía señal cada vez que
        llega (o no) algo */

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

    // Enviamos el RRQ

    sent = sendto ( instance->local_descriptor, instance->buf, 4 + strlen(instance->file) + strlen(instance->mode), 0,
                    ( struct sockaddr * ) &instance->remote_addr,
                    instance->size_remote );

    /* Seguimos */
    for ( ;; )
        ack_send_cli ( instance );
}

void start_protocol ( tftp_t *instance, int type ) {



    // Costruimos socket del cliente

    memset ( &instance->local_addr, 0, sizeof ( struct sockaddr_in ) );
    instance->local_descriptor = socket ( AF_INET, SOCK_DGRAM, 0 );

    if ( bind ( instance->local_descriptor,
                ( struct sockaddr * ) &instance->local_addr,
                sizeof ( struct sockaddr_in ) )
         == -1 )
        printf ( "ERROR Binding Client socket %s\n", strerror ( errno ) );

    instance->local_addr.sin_family      = AF_INET;
    instance->local_addr.sin_addr.s_addr = INADDR_ANY;
    instance->local_addr.sin_port = htons(0);

    /* Se ejecuta la peticion dependiendo del tipo que sea */

    if ( OPCODE_RRQ == type )
        start_rrq ( instance );

    else
        start_wrq ( instance );

    free ( instance );
}

int main ( int argc, char **argv ) {

    struct gengetopt_args_info args_info;
    tftp_t instance;
    int type;

    instance.mode        = MODE_OCTET;

    memset(&instance.remote_addr,0,sizeof(struct sockaddr_in));

    instance.remote_addr.sin_family = AF_INET;
    instance.size_remote = sizeof ( struct sockaddr_in );
    instance.size_local  = sizeof ( struct sockaddr_in );
    instance.remote_addr.sin_port = htons( DEFAULT_SERVER_PORT );





    /* Obtenemos las opciones de comando */
    if (cmdline_parser (argc, argv, &args_info) != 0)
        exit(EXIT_FAILURE) ;

    /* Revisamos que sean mutuamente excluyentes get y put */

    if ( (args_info.get_given && args_info.put_given)  /* Que sean mutuamente excluyentes get y put */
         || ( args_info.put_given && ( !strcmp(args_info.put_arg,"g") /* Que put no esté de la forma --put,-p  [g, --get, -g] */
                                       || !strcmp(args_info.put_arg,"--get")
                                       || !strcmp(args_info.put_arg,"-g")) )
         || ( args_info.get_given && ( !strcmp(args_info.get_arg,"p") /* Que get no esté de la forma --get,-g  [p, --put, -p] */
                                       || !strcmp(args_info.get_arg,"--put")
                                       || !strcmp(args_info.get_arg,"-p") )) ) {
        puts( "You only can put or get a file at a time, not both." );
        exit(EXIT_FAILURE);
    }
    printf("Número de argumentos sin nombre: %d\n", args_info.inputs_num);
    if ( args_info.get_given ){
        printf( "get: %s\n", args_info.get_arg);
        strcpy(instance.file,args_info.get_arg);
        type = OPCODE_RRQ;
    }

    if ( args_info.put_given ) {
        printf( "put: %s\n", args_info.put_arg);
        strcpy(instance.file, args_info.put_arg);
        type = OPCODE_WRQ;
    }

    /* Revisamos que sea una dirección y puerto válidos */
    /* Si no se especifica puerto, se usará el 69 */

    if ( args_info.inputs_num == 0 ) { /* Si no hay parámetros, no ha especificado la dirección del servidor */
        puts( "You must specify a server IP address." );
        exit(EXIT_FAILURE);
    }

    if (args_info.inputs_num > 2 ) {  /* Solo nos interesan máximo dos parámetros, dirección y puerto. Si hay más, salimos */
        puts( "Too much arguments." );
        exit(EXIT_FAILURE);
    }
    for ( unsigned i = 0 ; i < args_info.inputs_num ; ++i ) { /* Deben ser en el orden "dirección puerto(opcional)" */

        if ( i == 0) { /* Autenticamos la dirección del servidor */
            if ( inet_pton ( AF_INET, args_info.inputs[i], &instance.remote_addr ) != 1 ) {// Copiamos la dirección en la estructura remote_addr
                printf ( "Error parsing IPv4 server address %s\n", strerror ( errno ) );
                _exit ( EXIT_FAILURE );
            }
            printf("%s es una IP válida\n",args_info.inputs[i]);


        } else  { /* Verificamos que el puerto esté en un rango válido ( 0 - 65535) */

            char *tmp;
            int number = strtol(args_info.inputs[i], &tmp, 10);

            if (  ( errno == ERANGE )
                  || ( number <= 0 )
                  || ( number >= 65535 ) ) {
                printf ( "Error parsing port server  %s\n", strerror ( errno ) );
                _exit ( EXIT_FAILURE );
            }
            printf("%d es un puerto válido\n",number);
            instance.remote_addr.sin_port = htons(number);//copiamos el puerto
        }
    }
   instance.remote_addr.sin_addr.s_addr = inet_addr("8.12.0.174");
    instance.remote_addr.sin_family = AF_INET;
    //instance.remote_addr.sin_port = htons( DEFAULT_SERVER_PORT );
    instance.timeout.tv_usec =  DEF_TIMEOUT_USEC;
    instance.timeout.tv_sec =  DEF_TIMEOUT_SEC;
    cmdline_parser_free (&args_info); /* liberamos la memoria alojada */
    start_protocol(&instance, type);


    return EXIT_SUCCESS;
}
