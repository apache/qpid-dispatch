#include <proton/codec.h>
#include <proton/delivery.h>
#include <proton/engine.h>
#include <proton/event.h>
#include <proton/listener.h>
#include <proton/message.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <proton/types.h>
#include <proton/version.h>

#include <inttypes.h>
#include <memory.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


static
double
get_timestamp ( void )
{
  struct timeval t;
  gettimeofday ( & t, 0 );
  return t.tv_sec + ((double) t.tv_usec) / 1000000.0;
}




#define MAX_NAME   100
#define MAX_ADDRS  1000
#define MAX_MESSAGE 1000000


typedef
struct addr_s
{
  pn_link_t * link;
  char      * path;
  int         messages;
}
addr_t,
* addr_p;





typedef 
struct context_s 
{
  addr_t            addrs [ MAX_ADDRS ];
  int               n_addrs;

  int               link_count;
  char              name [ MAX_NAME ];
  int               sending;
  char              id [ MAX_NAME ];
  char              host [ MAX_NAME ];
  size_t            max_send_length,
                    max_receive_length,
                    message_length,
                    outgoing_buffer_size;
  char            * outgoing_buffer;
  char              incoming_message [ MAX_MESSAGE ];   // BUGALERT
  char            * port;
  char            * log_file_name;
  FILE            * log_file;
  int               messages_sent;
  
  int               received,
                    accepted,
                    rejected,
                    released,
                    modified;

  pn_message_t    * message;
  uint64_t          total_bytes_sent,
                    total_bytes_received;
  //
  // expected_messages is per address.
  // total_expected_messages is for all of them put together.
  int               expected_messages;
  int               total_expected_messages;

  size_t            credit_window;
  pn_proactor_t   * proactor;
  pn_listener_t   * listener;
  pn_connection_t * connection;
  int               throttle;
  int               delay;

  double          * flight_times;
  double          * time_stamps;
  int               max_flight_times;
  int               n_flight_times;
  char              flight_times_file_name [ 1000 ];

  double            grand_start_time,
                    send_start_time,
                    stop_time;

  bool              doing_throughput;
}
context_t,
* context_p;





context_p context_g = 0;


void
log ( context_p context, char const * format, ... )
{
  if ( ! context->log_file )
    return;

  fprintf ( context->log_file, "%.6f  ", get_timestamp() );
  va_list ap;
  va_start ( ap, format );
  vfprintf ( context->log_file, format, ap );
  va_end ( ap );
  fflush ( context->log_file );
}





void
dump_flight_times ( context_p context )
{
  // Only receivers store and then dump their flight times.
  if ( context->sending ) 
  {
    return;
  }

  if ( context->n_flight_times <= 0 )
  {
    log ( context, "error: receiver has no flight times to dump.\n" );
    return;
  }

  char default_file_name[1000];
  char * file_name = context->flight_times_file_name;

  if ( file_name == 0 )
  {
    sprintf ( default_file_name, "/tmp/flight_times_%d", getpid() );
    file_name = default_file_name;
  }

  FILE * fp = fopen ( file_name, "w" );
  for ( int i = 0; i < context->n_flight_times; i ++ )
  {
    fprintf ( fp, 
              "%.6lf %.7lf\n", 
              context->time_stamps  [ i ],
              context->flight_times [ i ] * 1000  // write the flight time in msec
            );
  }
  fclose ( fp );
}





int
find_addr ( context_p context, pn_link_t * target_link )
{
  for ( int i = 0; i < context->n_addrs; i ++ )
  {
    if ( target_link == context->addrs[i].link ) 
      return i;
  }
  
  return -1;
}





int
rand_int ( int one_past_max )
{
  double zero_to_one = (double) rand() / (double) RAND_MAX;
  return (int) (zero_to_one * (double) one_past_max);
}





void
make_random_message ( context_p context )
{
  context->message_length = rand_int ( context->max_send_length );
  for ( int i = 0; i < context->message_length; ++ i )
    context->outgoing_buffer [ i ] = uint8_t ( rand_int ( 256 ) );
}





void
make_timestamped_message ( context_p context )
{
  double ts = get_timestamp();

  context->message_length = 100;
  memset ( context->outgoing_buffer, 'x', context->message_length );
  sprintf ( context->outgoing_buffer, "%.7lf", ts );
}





size_t 
encode_outgoing_message ( context_p context ) 
{
  int err = 0;
  size_t size = context->outgoing_buffer_size;

  if ( 0 == (err = pn_message_encode ( context->message, context->outgoing_buffer, & size) ) )
    return size;

  if ( err == PN_OVERFLOW ) 
  {
    log ( context, "error : overflowed outgoing_buffer_size == %d\n", context->outgoing_buffer_size );
    exit ( 1 );
  } 
  else
  if ( err != 0 ) 
  {
    log ( context, 
          "error : encoding message: %s |%s|\n", 
          pn_code ( err ), 
          pn_error_text(pn_message_error ( context->message ) ) 
        );
    exit ( 1 );
  }

  return 0; // unreachable   // I think.
}





void
store_flight_time ( context_p context, double flight_time, double recv_time ) 
{
  if ( context->n_flight_times >= context->max_flight_times ) 
    return;

  context->flight_times [ context->n_flight_times ] = flight_time;
  context->time_stamps  [ context->n_flight_times ] = recv_time;
  context->n_flight_times ++;
}





void
write_report ( )
{
  dump_flight_times ( context_g );
}





void 
decode_message ( context_p context, pn_delivery_t * delivery ) 
{
  double receive_timestamp = get_timestamp();

  pn_message_t * msg  = context->message;
  pn_link_t    * link = pn_delivery_link ( delivery );
  ssize_t        incoming_size = pn_delivery_pending ( delivery );

  if ( incoming_size >= context->max_receive_length )
  {
    log ( context, "error : incoming message too big: %d.\n", incoming_size );
    exit ( 1 );
  }

  pn_link_recv ( link, context->incoming_message, incoming_size);
  pn_message_clear ( msg );

  if ( pn_message_decode ( msg, context->incoming_message, incoming_size ) ) 
  {
    log ( context, 
          "error : from pn_message_decode: |%s|\n",
          pn_error_text ( pn_message_error ( msg ) )
        );
    exit ( 2 );
  }
  else
  {
    char temp[1000];
    char * dst = temp;
    pn_string_t *s = pn_string ( NULL );
    pn_inspect ( pn_message_body(msg), s );
    //log ( context, "%s\n", pn_string_get(s));
    double send_timestamp;
    const char * message_content = pn_string_get(s);
    const char * src = message_content + 1; // first char is a double-quote!

    while ( *src != 'x' && *src != '\\' )
      * dst ++ = * src ++;
    * dst = 0;

    sscanf ( temp, "%lf", & send_timestamp );
    context->total_bytes_received += strlen ( message_content );
    pn_free ( s );

    double flight_time = receive_timestamp - send_timestamp;
    store_flight_time ( context, flight_time, receive_timestamp );

    if ( context->n_flight_times >= context->max_flight_times )
    {
      // Only receivers record their flight times.
      if ( ! context->sending ) 
      {
        // Use the same delay that we use at start-up, here at the
        // end to avoid dumping stats while other clients are still
        // running.
        int delay = context->delay;
        log ( context, "Dumping flight times in %d seconds.\n", delay );
        struct itimerval timer;
        timer.it_value.tv_sec  = delay;
        timer.it_value.tv_usec =  0;
        timer.it_interval = timer.it_value;
        signal ( SIGALRM, (void (*)(int)) write_report );
        setitimer ( ITIMER_REAL, & timer, NULL );
      }
    }
  }
}





void 
send_message ( context_p context ) 
{
  double now = get_timestamp();
  double time_since_start = now - context->grand_start_time;

  // This is the enforced delay to prevent senders from sending
  // // while other senders are still attaching.
  if ( time_since_start < context->delay ) 
  {
    log ( context, "too soon to send: %.3lf\n", time_since_start );
    // Gotta pause if it's still too soon to send,
    // or we will spend a lot of cycles just doing this.
    sleep ( 1 );
    return;
  }

  // We are about to send a message.
  // If this is the first one, record this as the start-time
  // to be used for throughput measurement.
  if ( context->messages_sent == 0 )
  {
    context->send_start_time = get_timestamp();
  }

  for ( int i = 0; i < context->n_addrs; i ++ )
  {
    pn_link_t * link = context->addrs[i].link;

    if ( ! link )
    {
      // No send link yet.
      return;
    }

   // Set messages ID from sent count.
    pn_atom_t id_atom;
    char id_string [ 20 ];
    sprintf ( id_string, "%d", context->messages_sent );
    id_atom.type = PN_STRING;
    id_atom.u.as_bytes = pn_bytes ( strlen(id_string), id_string );
    pn_message_set_id ( context->message, id_atom );


    // make_random_message ( context );
    make_timestamped_message ( context );
    pn_data_t * body = pn_message_body ( context->message );
    pn_data_clear ( body );
    pn_data_enter ( body );
    pn_bytes_t bytes = { context->message_length, context->outgoing_buffer };
    pn_data_put_string ( body, bytes );
    pn_data_exit ( body );
    size_t outgoing_size = encode_outgoing_message ( context );

    pn_delivery ( link, 
                  pn_dtag ( (const char *) & context->messages_sent, sizeof(context->messages_sent) ) 
                );
    pn_link_send ( link, 
                   context->outgoing_buffer, 
                   outgoing_size 
                 );

    if ( context->messages_sent == 0 )
    {
      log ( context, "first_send\n" );
    }

    context->messages_sent ++;

    /*
    if ( ! ( context->messages_sent % 1000 ) )
    {
      log ( context, "sent %d\n", context->messages_sent );
    }
    */

    // log ( context, "sent: %d\n", context->messages_sent );
    pn_link_advance ( link );
    context->total_bytes_sent += outgoing_size;
  }
}





bool 
process_event ( context_p context, pn_event_t * event ) 
{
  pn_session_t   * event_session;
  pn_transport_t * event_transport;
  pn_link_t      * event_link;
  pn_delivery_t  * event_delivery;

  char link_name [ 1000 ];


  switch ( pn_event_type( event ) ) 
  {
    case PN_LISTENER_ACCEPT:
      context->connection = pn_connection ( );
      pn_listener_accept ( pn_event_listener ( event ), context->connection );
    break;


    case PN_CONNECTION_INIT:
      snprintf ( context->id, MAX_NAME, "%d_%d", int(getpid()), int(get_timestamp()) );
      log ( context, "connection id is |%s|\n", context->id );
      pn_connection_set_container ( pn_event_connection( event ), context->id );
      event_session = pn_session ( pn_event_connection( event ) );
      pn_session_open ( event_session );

      if ( context->sending )
      {
        for ( int i = 0; i < context->n_addrs; i ++ )
        {
          sprintf ( link_name, "%d_send_%05d", getpid(), i );
          context->addrs[i].link = pn_sender (  event_session, link_name );
          pn_terminus_set_address ( pn_link_target(context->addrs[i].link), context->addrs[i].path );
          pn_link_set_snd_settle_mode ( context->addrs[i].link, PN_SND_UNSETTLED );
          pn_link_set_rcv_settle_mode ( context->addrs[i].link, PN_RCV_FIRST );

          pn_link_open ( context->addrs[i].link );
        }

      }
      else
      {
        for ( int i = 0; i < context->n_addrs; i ++ )
        {
          sprintf ( link_name, "%d_recv_%05d", getpid(), i );
          context->addrs[i].link = pn_receiver( event_session, link_name );
          pn_terminus_set_address ( pn_link_source(context->addrs[i].link), context->addrs[i].path );
          pn_link_open ( context->addrs[i].link );
        }
      }

    break;


    case PN_CONNECTION_BOUND: 
      event_transport = pn_event_transport ( event );
      pn_transport_require_auth ( event_transport, false );
      pn_sasl_allowed_mechs ( pn_sasl(event_transport), "ANONYMOUS" );
    break;


    case PN_CONNECTION_REMOTE_OPEN : 
      pn_connection_open ( pn_event_connection( event ) ); 
    break;


    case PN_SESSION_REMOTE_OPEN:
      pn_session_open ( pn_event_session( event ) );
    break;


    case PN_LINK_REMOTE_OPEN: 
      event_link = pn_event_link( event );
      pn_link_open ( event_link );
      if ( pn_link_is_receiver ( event_link ) )
      {
        pn_link_flow ( event_link, context->credit_window );
        // log ( context, "receiver sent flow of %d\n", context->credit_window );
      }
    break;


    case PN_CONNECTION_WAKE:
    {
      if ( context->throttle > 0 )
      {
        if ( context->messages_sent < context->total_expected_messages )
        {
          send_message ( context );
          pn_proactor_set_timeout ( context->proactor, context->throttle );
        }
      }
      break;
    }


    case PN_PROACTOR_TIMEOUT:
    {
      if ( context->throttle > 0 )
      {
        pn_connection_wake ( context->connection );
      }
      break;
    }


    case PN_LINK_FLOW : 
    {
      event_link = pn_event_link ( event );

      if ( context->throttle > 0 )
      {
        if ( context->messages_sent < context->total_expected_messages )
        {
          if ( pn_link_is_sender(event_link) )
          {
            pn_proactor_set_timeout ( context->proactor, context->throttle );
          }
        }
      }
      else
      {
        if ( pn_link_is_sender(event_link) && context->messages_sent < context->total_expected_messages )
        {
          while ( pn_link_credit ( event_link ) > 0 && context->messages_sent < context->total_expected_messages )
            send_message ( context );
        }
      }
    }
    break;


    case PN_DELIVERY: 
      event_delivery = pn_event_delivery( event );
      event_link = pn_delivery_link ( event_delivery );

      if ( pn_link_is_sender ( event_link ) ) 
      {
        int state = pn_delivery_remote_state(event_delivery);
        switch ( state ) 
        {
          case PN_RECEIVED:
            context->received ++;
          break;

          case PN_ACCEPTED:
            context->accepted ++;

            /*
            if (! (context->accepted % 1000 ) )
            {
              log ( context, "accepted: %d\n", context->accepted );
            }
            */

          break;

          case PN_REJECTED:
            context->rejected ++;
          break;

          case PN_RELEASED:
            context->released ++;
          break;

          case PN_MODIFIED:
            context->modified ++;
          break;

          default:
            log ( context, "error : unknown remote state! %d\n", state );
          break;
        }

        pn_delivery_settle ( event_delivery );

        if ( context->accepted + context->released + context->modified >= context->total_expected_messages) 
        {
          // Calculate throughput.
          context->stop_time = get_timestamp();
          double duration = context->stop_time - context->send_start_time;
          double messages_per_second = double(context->total_expected_messages) / duration;
          log ( context, "throughput: %.3lf messages per second.\n", messages_per_second );

          if ( context->connection )
            pn_connection_close(context->connection);
          if ( context->listener )
            pn_listener_close(context->listener);
          break;
        }
      }
      else 
      if ( pn_link_is_receiver ( event_link ) )
      {
        if ( ! pn_delivery_readable  ( event_delivery ) )
          break;

        if ( pn_delivery_partial ( event_delivery ) ) 
          break;

        decode_message ( context, event_delivery );
        pn_delivery_update ( event_delivery, PN_ACCEPTED );
        pn_delivery_settle ( event_delivery );

        // As the receiver, we only count that a message has been received.
        context->received ++;

        //if ( ! (context->received % 100 ) )
          //log ( context, "received %d\n", context->received );

        int index = find_addr ( context, event_link );
        if ( index < 0 )
        {
          fprintf ( stderr, "client error: unknown link in delivery.\n" );
          exit ( 1 );
        }
        else
        {
          context->addrs[index].messages ++;
        }


        if ( context->received >= context->total_expected_messages) 
        {
          if ( context->connection )
            pn_connection_close(context->connection);
          if ( context->listener )
            pn_listener_close(context->listener);
          break;
        }
        pn_link_flow ( event_link, context->credit_window - pn_link_credit(event_link) );
      }
      else
      {
        fprintf ( stderr, 
                  "A delivery came to a link that is not a sender or receiver.\n" 
                );
        exit ( 1 );
      }
    break;


    case PN_CONNECTION_REMOTE_CLOSE :
      pn_connection_close ( pn_event_connection( event ) );
    break;

    case PN_SESSION_REMOTE_CLOSE :
      pn_session_close ( pn_event_session( event ) );
    break;


    case PN_LINK_REMOTE_CLOSE :
      pn_link_close ( pn_event_link( event ) );
    break;


    case PN_PROACTOR_INACTIVE:
      return false;

    default:
      break;
  }

  return true;
}





void
init_context ( context_p context, int argc, char ** argv )
{
  #define NEXT_ARG      argv[i+1]

  strcpy ( context->name, "default_name" );
  strcpy ( context->host, "0.0.0.0" );

  context->listener                = 0;
  context->connection              = 0;
  context->proactor                = 0;

  context->sending                 = 0;
  context->link_count              = 0;
  context->messages_sent           = 0;

  context->received                = 0;
  context->accepted                = 0;
  context->rejected                = 0;
  context->released                = 0;
  context->modified                = 0;

  context->log_file_name           = 0;
  context->log_file                = 0;
  context->message                 = 0;
  context->total_bytes_sent        = 0;
  context->total_bytes_received    = 0;

  context->expected_messages       = 0;
  context->total_expected_messages = 0;
  context->credit_window           = 1000;
  context->max_send_length         = 100;

  context->throttle                = 0;
  context->delay                   = 0;

  context->n_addrs                 = 0;

  context->grand_start_time        = get_timestamp();

  context->doing_throughput        = false;


  for ( int i = 1; i < argc; ++ i )
  {

    // throttle ----------------------------------------------
    if ( ! strcmp ( "--throttle", argv[i] ) )
    {
      context->throttle = atoi(NEXT_ARG);
      i ++;
    }
    // delay ----------------------------------------------
    else
    if ( ! strcmp ( "--delay", argv[i] ) )
    {
      context->delay = atoi(NEXT_ARG);
      i ++;
    }
    // address ----------------------------------------------
    else
    if ( ! strcmp ( "--address", argv[i] ) )
    {
      context->addrs[context->n_addrs].path     = strdup ( NEXT_ARG );
      context->addrs[context->n_addrs].link     = 0;
      context->addrs[context->n_addrs].messages = 0;
      context->n_addrs ++;
      i ++;
    }
    // operation ----------------------------------------------
    else
    if ( ! strcmp ( "--operation", argv[i] ) )
    {
      if ( ! strcmp ( "send", argv[i+1] ) )
      {
        context->sending = 1;
      }
      else
      if ( ! strcmp ( "receive", NEXT_ARG ) )
      {
        context->sending = 0;
      }
      else
      {
        fprintf ( stderr, "value for --operation should be 'send' or 'receive'.\n" );
        exit ( 1 );
      }
      
      i ++;
    }
    // name ----------------------------------------------
    else
    if ( ! strcmp ( "--name", argv[i] ) )
    {
      if ( ! strcmp ( NEXT_ARG, "PID" ) )
      {
        sprintf ( context->name, "client_%d", getpid() );
      }
      else
      {
        memset  ( context->name, 0, MAX_NAME );
        strncpy ( context->name, NEXT_ARG, MAX_NAME );
      }

      i ++;
    }
    // max_message_length ----------------------------------------------
    else
    if ( ! strcmp ( "--max_message_length", argv[i] ) )
    {
      context->max_send_length = atoi ( NEXT_ARG );
      i ++;
    }
    // port ----------------------------------------------
    else
    if ( ! strcmp ( "--port", argv[i] ) )
    {
      context->port = strdup ( NEXT_ARG );
      i ++;
    }
    // log ----------------------------------------------
    else
    if ( ! strcmp ( "--log", argv[i] ) )
    {
      context->log_file_name = strdup ( NEXT_ARG );
      i ++;
    }
    // flight_times_file_name ----------------------------------------------
    else
    if ( ! strcmp ( "--flight_times_file_name", argv[i] ) )
    {
      sprintf ( context->flight_times_file_name, 
                "%s/%s_flight_times", 
                NEXT_ARG, 
                context->name 
              );
      i ++;
    }
    // messages ----------------------------------------------
    else
    if ( ! strcmp ( "--messages", argv[i] ) )
    {
      context->expected_messages = atoi ( NEXT_ARG );
      i ++;
    }
    // throughput ----------------------------------------------
    else
    if ( ! strcmp ( "--throughput", argv[i] ) )
    {
      context->doing_throughput = true;
    }
    // unknown ----------------------------------------------
    else
    {
      fprintf ( stderr, "Unknown option: |%s|\n", argv[i] );
      exit ( 1 );
    }
  }
}





int 
main ( int argc, char ** argv ) 
{

  srand ( getpid() );
  context_t context;
  context_g = & context;
  init_context ( & context, argc, argv );

  if ( context.log_file_name ) 
  {
    context.log_file = fopen ( context.log_file_name, "w" );
  }
  log ( & context, "start\n" );

  if ( context.max_send_length <= 0 )
  {
    fprintf ( stderr, "no max message length.\n" );
    exit ( 1 );
  }

  context.total_expected_messages = context.expected_messages * context.n_addrs;
  log ( & context, "total_expected_messages == %d\n", context.total_expected_messages );
  context.flight_times    = (double *) malloc ( sizeof(double) * context.total_expected_messages );
  context.time_stamps     = (double *) malloc ( sizeof(double) * context.total_expected_messages );
  context.max_flight_times = context.total_expected_messages;
  context.n_flight_times   = 0;

  // Make the max send length larger than the max receive length 
  // to account for the extra header bytes.
  context.max_receive_length   = 1000;
  context.outgoing_buffer_size = 1000;
  context.outgoing_buffer = (char *) malloc ( context.outgoing_buffer_size );

  context.message = pn_message();


  char addr[PN_MAX_ADDR];
  pn_proactor_addr ( addr, sizeof(addr), context.host, context.port );
  context.proactor   = pn_proactor();
  context.connection = pn_connection();
  pn_proactor_connect ( context.proactor, context.connection, addr );

  int batch_done = 0;
  while ( ! batch_done ) 
  {
    pn_event_batch_t *events = pn_proactor_wait ( context.proactor );
    pn_event_t * event;
    for ( event = pn_event_batch_next(events); event; event = pn_event_batch_next(events)) 
    {
      if (! process_event( & context, event ))
      {
        batch_done = 1;
        break;
       }
    }
    pn_proactor_done ( context.proactor, events );
  }

  write_report ( );

  return 0;
}





