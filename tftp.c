#include "tftp.h"

void err_log_exit( int priority, const char *format, ... ) {

    va_list args;

    va_start(args, format);
    vsyslog(priority, format, args );
    va_end(args);

    syslog(LOG_CRIT, "FATAL ERROR. Shutting down server ...");
    _exit(EXIT_FAILURE);
}

void _err_log_exit( int priority, const char *format, ... ) {

    va_list args;
    
    va_start(args, format);
    vsyslog(priority, format, args);
    va_end(args);
    
    syslog(LOG_CRIT, "FATAL ERROR. Killing child ...");
    _exit(EXIT_FAILURE);
}

void build_data_msg( tftp_t * instance ) {

    u_char *p;
    memset( &instance->buf, 0, MAX_BUFSIZE );
    p = instance->buf;
    *p = ( OPCODE_DATA >> 8 ) & 0xff;      p++;
    *p =   OPCODE_DATA & 0xff;             p++;
    *p = ( instance->blknum >> 8 ) & 0xff; p++;
    *p = ( instance->blknum ) & 0xff;      p++;
    memcpy( p, instance->msg, BUFSIZE );
}

void build_error( tftp_t * instance) {

    u_char *p;
    memset( &instance->buf, 0, MAX_BUFSIZE );
    p = instance->buf;
    *p = ( OPCODE_ERROR >> 8 )  & 0xff;   p++;
    *p =   OPCODE_ERROR & 0xff;           p++;
    *p = ( instance->err >> 8 ) & 0xff;   p++;
    *p =   instance->err & 0xff;          p++;
    memcpy( p, instance->msgerr, strlen( instance->msgerr ) );

}

void build_ack_msg( tftp_t * instance )  {

    u_char *p;
    memset( instance->buf, 0, MAX_BUFSIZE );
    p = instance->buf;
    *p = ( OPCODE_ACK >> 8 ) & 0xff;        p++;
    *p =   OPCODE_ACK & 0xff;               p++;
    *p = ( instance->blknum >> 8 ) & 0xff;  p++;
    *p =   instance->blknum & 0xff;         p++;
    memset( p, 0, BUFSIZE );

}

void dec_data( tftp_t * instance ) {

    u_char *p;
    p = instance->buf;
    p += 4;
    memcpy( instance->msg, p, BUFSIZE );

}

