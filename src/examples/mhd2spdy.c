/*
    Copyright (C) 2013 Andrey Uzunov

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file mhd2spdy.c
 * @brief  The main file of the HTTP-to-SPDY proxy with the 'main' function
 *         and event loop. No threads are used.
 *         Currently only GET is supported.
 *         TODOs:
 *         - non blocking SSL connect
 *         - check certificate
 * @author Andrey Uzunov
 */
 
 
#include "mhd2spdy_structures.h"
#include "mhd2spdy_spdy.h"
#include "mhd2spdy_http.h"

static int run = 1;
//static int spdy_close = 0;

static void catch_signal(int signal)
{
  (void)signal;
  //spdy_close = 1;
  run = 0;
}

int
run_everything ()
{	
  unsigned long long timeoutlong=0;
  struct timeval timeout;
  int ret;
  fd_set rs;
  fd_set ws;
  fd_set es;
  int maxfd = -1;
  int maxfd_s = -1;
  struct MHD_Daemon *daemon;
  nfds_t spdy_npollfds = 1;
  //struct pollfd spdy_pollfds[MAX_SPDY_CONNECTIONS];
  struct URI * spdy2http_uri = NULL;
  //int spdy_nfds;
  //int spdylay_timeout = 0;
  struct SPDY_Connection *connection;
  struct SPDY_Connection *connections[MAX_SPDY_CONNECTIONS];
  struct SPDY_Connection *connection_for_delete;

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    PRINT_INFO("signal failed");
    
  if (signal(SIGINT, catch_signal) == SIG_ERR)
    PRINT_INFO("signal failed");

  //spdy2http_url = argv[2];
  glob_opt.streams_opened = 0;
  glob_opt.responses_pending = 0;
  glob_opt.global_memory = 0;
  //spdy_proto_version = 0;

  srand(time(NULL));
  
  if(init_parse_uri(&glob_opt.uri_preg))
    DIE("Regexp compilation failed");
    
    
  if(NULL != glob_opt.spdy2http_str)
  {
    ret = parse_uri(&glob_opt.uri_preg, glob_opt.spdy2http_str, &spdy2http_uri);
    if(ret != 0)
      DIE("spdy_parse_uri failed");
  }

  SSL_load_error_strings();
  SSL_library_init();
  glob_opt.ssl_ctx = SSL_CTX_new(SSLv23_client_method());
  if(glob_opt.ssl_ctx == NULL) {
    PRINT_INFO2("SSL_CTX_new %s", ERR_error_string(ERR_get_error(), NULL));
    abort();
  }
  spdy_ssl_init_ssl_ctx(glob_opt.ssl_ctx, &glob_opt.spdy_proto_version);

  daemon = MHD_start_daemon ( 
          MHD_SUPPRESS_DATE_NO_CLOCK,
          glob_opt.listen_port,
          NULL, NULL, &http_cb_request, NULL,
          MHD_OPTION_URI_LOG_CALLBACK, &http_log_cb, NULL,
          MHD_OPTION_END);
  if(NULL==daemon)
    DIE("MHD_start_daemon failed");


  do
  {

  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  
    if(NULL == glob_opt.spdy_connection && NULL != glob_opt.spdy2http_str)
    {
      glob_opt.spdy_connection = spdy_connect(spdy2http_uri, spdy2http_uri->port, strcmp("https", spdy2http_uri->scheme)==0);
      if(NULL == glob_opt.spdy_connection && glob_opt.only_proxy)
        PRINT_INFO("cannot connect to the proxy");
    }

    //PRINT_INFO("while1");
    FD_ZERO(&rs);
    FD_ZERO(&ws);
    FD_ZERO(&es);
    
    /*if(glob_opt.spdy_data_received)
        {
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
  glob_opt.spdy_data_received = false;
}
else{*/
    /*if(glob_opt.responses_pending || glob_opt.streams_opened)// TODO only streams_opened true?
      timeout.tv_usec = 0; //return immediately
    else
    {*/
      ret = MHD_get_timeout(daemon, &timeoutlong);
      if(MHD_NO == ret || timeoutlong > 5000)
        timeout.tv_sec = 5;
      else
      {
        timeout.tv_sec = timeoutlong / 1000;
        timeout.tv_usec = (timeoutlong % 1000) * 1000;
      }
    //}
//}
    if(MHD_NO == MHD_get_fdset (daemon,
                                  &rs,
                                  &ws, 
                                  &es,
                                  &maxfd))
    {
      PRINT_INFO("MHD_get_fdset error");
    }
    assert(-1 != maxfd);
    
    maxfd_s = spdy_get_selectfdset(
                                  &rs,
                                  &ws, 
                                  &es,
                                  connections, MAX_SPDY_CONNECTIONS, &spdy_npollfds);
    if(maxfd_s > maxfd) maxfd = maxfd_s;
 
    PRINT_INFO2("MHD timeout %lld %lld", (unsigned long long)timeout.tv_sec, (unsigned long long)timeout.tv_usec);
    //TODO
    //timeout.tv_sec = 0;
    //timeout.tv_usec = 0;

    glob_opt.spdy_data_received = false;
      
    ret = select(maxfd+1, &rs, &ws, &es, &timeout);
    PRINT_INFO2("timeout now %lld %lld", (unsigned long long)timeout.tv_sec, (unsigned long long)timeout.tv_usec);

    switch(ret)
    {
      case -1:
        PRINT_INFO2("select error: %i", errno);
        break;
      case 0:
        break;
      default:
      PRINT_INFO("run");
        //MHD_run_from_select(daemon,&rs, &ws, &es); //not closing FDs
        MHD_run(daemon);
        spdy_run_select(&rs, &ws, &es, connections, spdy_npollfds);
        if(glob_opt.spdy_data_received)
        {
          PRINT_INFO("MHD run again");
          //MHD_run_from_select(daemon,&rs, &ws, &es); //not closing FDs
          MHD_run(daemon);
        }
        break;
    }
    
    /*
    //if(glob_opt.streams_opened) spdylay_timeout = 500;
    //if(glob_opt.responses_pending || glob_opt.streams_opened) spdylay_timeout = 0;
    //else spdylay_timeout = 0;
    //else spdylay_timeout = 0;
    
    spdy_get_pollfdset(spdy_pollfds, connections, MAX_SPDY_CONNECTIONS, &spdy_npollfds);
    
    //TODO
    //spdylay_timeout = 0;
    
    PRINT_INFO2("spdylay timeout %i", spdylay_timeout);
    ret = poll(spdy_pollfds, spdy_npollfds, spdylay_timeout);
    if(ret == -1)
      DIE("poll");
    if(ret > 0){
      PRINT_INFO("spdy_run");
    spdy_run(spdy_pollfds, connections, spdy_npollfds); 
    }*/   
  }
  while(run);

  //TODO exit from loop and clean

  MHD_stop_daemon (daemon);
  
  //TODO SSL_free brakes
  spdy_free_connection(glob_opt.spdy_connection);
  
  connection = glob_opt.spdy_connections_head;
  while(NULL != connection)
  {    
    connection_for_delete = connection;
    connection = connection_for_delete->next;
    glob_opt.streams_opened -= connection_for_delete->streams_opened;
    DLL_remove(glob_opt.spdy_connections_head, glob_opt.spdy_connections_tail, connection_for_delete);
    spdy_free_connection(connection_for_delete);
  }
  
  free_uri(spdy2http_uri);
  
  deinit_parse_uri(&glob_opt.uri_preg);
  
  SSL_CTX_free(glob_opt.ssl_ctx);
  ERR_free_strings();
  EVP_cleanup();
    
  PRINT_INFO2("spdy streams: %i; http requests: %i", glob_opt.streams_opened, glob_opt.responses_pending);
  PRINT_INFO2("memory allocated %zu bytes", glob_opt.global_memory);

  return 0;
}

void
display_usage()
{
  printf(
    "Usage: http2spdy [-vo] [-b <SPDY2HTTP-PROXY>] -p <PORT>\n"
    "TODO\n"
  );
}


int
main (int argc,
      char *const *argv)
{   
  int getopt_ret;
  int option_index;
  struct option long_options[] = {
    {"port",  required_argument, 0, 'p'},
    {"backend-proxy",  required_argument, 0, 'b'},
    {"verbose",  no_argument, 0, 'v'},
    {"only-proxy",  no_argument, 0, 'o'},
    {0, 0, 0, 0}
  };
  
  while (1)
  {
    getopt_ret = getopt_long( argc, argv, "p:b:vo", long_options, &option_index);
    if (getopt_ret == -1)
      break;

    switch(getopt_ret)
    {
      case 'p':
        glob_opt.listen_port = atoi(optarg);
        break;
        
      case 'b':
        glob_opt.spdy2http_str = strdup(optarg);
        if(NULL == glob_opt.spdy2http_str)
          return 1;
        break;
        
      case 'v':
        glob_opt.verbose = true;
        break;
        
      case 'o':
        glob_opt.only_proxy = true;
        break;
        
      case 0:
        PRINT_INFO("0 from getopt");
        break;
        
      case '?':
        display_usage();
        return 1;
        
      default:
        DIE("default from getopt");
    }
  }
  
  if(
    0 == glob_opt.listen_port
    || (glob_opt.only_proxy && NULL == glob_opt.spdy2http_str)
    )
  {
    display_usage();
    return 1;
  }
    
  return run_everything();
}
