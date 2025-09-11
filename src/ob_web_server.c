/*
 * Copyright 2024 Beechwoods Software, Inc brad@beechwoods.com
 * All Rights Reserved
 * SPDX-License-Identifier: Apache 2.0
 */


#include <zephyr/kernel.h>
#include <errno.h>
#include <ctype.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/wifi.h>

#include "ob_web_server.h"
#include "ob_wifi.h"
#include "ob_nvs_data.h"
#include "ob_certs.h"


#ifdef CONFIG_ONBOARDING_WEB_SERVER
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(ONBOARDING_LOG_MODULE_NAME, CONFIG_ONBOARDING_LOG_LEVEL);


/** @brief work structure for the web server thread */
static struct k_work start_web_server_work;

/** @brief inidicates if the web server has been initialized */
bool web_server_inited= false;

/** @brief indicates if the web server has been started */
static bool mWebServerRunning = false;

/** @brief linked list of registered web pages */
web_page_t * web_pages = NULL;

#if defined(CONFIG_ONBOARDING_WEB_SERVER_HTTPS)
#define MY_PORT 443
static sec_tag_t sec_tag_list[] = {
  CONFIG_ONBOARDING_WEB_SERVER_CREDENTIALS_TAG
};
#else // CONFIG_ONBOARDING_WEB_SERVER_HTTPS
/** @brief port for the web server to listen on */
#define MY_PORT 80
#endif

/** @brief the maximum size of the content item in the http header */
#define MAX_HEADER_CONTENT_LEN 40
/** @brief the size of "Content Length:" in the http header */
#define CONTENT_LENGTH_END 15

/** @brief the priority for the tcp processing  threads */
#if defined(CONFIG_NET_TC_THREAD_COOPERATIVE)
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#else
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#endif

/** @brief the start of the head http element */
static char content_head[] = {
  "<html>\n<head>\n<title>"
};
/** @brief the tail of the head http element */
static char content_head_tail[] = {"</title>\n</head>\n<body>\n" };

/** @brief the OK http header */
static const char http1_1_OK[] = "HTTP/1.1 200 OK\r\n";

/** @brief the content length http content header */
static const char content_length[] = "Content-Length: %d\r\n";
/** @brief the content type http content header */
static const char CONTENT_TYPE[] = "Content-Type: text/html; charset=UTF-8\r\n\r\n";

/** @brief the not found error response header */
static const char http1_1_404[] = "HTTP/1.1 404 Not Found\r\nContent-Length=162\r\n\r\n<html><head><title>404 Not Found</title></head>\n<body bgcolor=\"white\"><center><h1>404 Not Found</h1></center><hr><center>nginx/0.8.54</center></body></html>";

/** @brief the server error error response header */
static const char http1_1_500[] = "HTTP/1.1 500 Internal Server Error\r\nContent-Length=199\r\n\r\n<html><head><title>500 Internal Server Error</title></head>\n<body bgcolor=\"white\"><center><h1>500 Internal Server Error</h1></center><hr><center>nginx/0.8.54</center></body></html>";

/** @brief The maximum backlog for the socket */
#define MAX_CLIENT_QUEUE CONFIG_HTTP_NUM_HANDLERS

#if defined(CONFIG_NET_IPV4)
/** @brief an array of CONFIG_HTTP_NUM_HANDLERS stacks for handling tcp over ipV4 */
K_THREAD_STACK_ARRAY_DEFINE(tcp4_handler_stack, CONFIG_HTTP_NUM_HANDLERS,
                            CONFIG_ONBOARDING_WEB_STACK_SIZE);
/** @brief an array to hold the threads for processing tcp over IPV4 */
static struct k_thread tcp4_handler_thread[CONFIG_HTTP_NUM_HANDLERS];

/** @brief Array of Thread IDs of the active tcp over IPV4 threads */
static k_tid_t tcp4_handler_tid[CONFIG_HTTP_NUM_HANDLERS];
#endif

#if defined(CONFIG_NET_IPV6)
K_THREAD_STACK_ARRAY_DEFINE(tcp6_handler_stack, CONFIG_HTTP_NUM_HANDLERS,
			    CONFIG_ONBOARDING_WEB_STACK_SIZE);
static struct k_thread tcp6_handler_thread[CONFIG_HTTP_NUM_HANDLERS];
static k_tid_t tcp6_handler_tid[CONFIG_HTTP_NUM_HANDLERS];
#endif

/** @brief Management callback structure to obtain network management callbacks */
static struct net_mgmt_event_callback mgmt_cb;

/** @brief Indicates if the network has been brought up */
static bool connected;

/** @brief a semaphore that is released when thenetwork is up */
static K_SEM_DEFINE(run_app, 0, 1);

/** @brief an indicator that quit has been signalled */
static bool want_to_quit = false;

#ifdef CONFIG_NET_IPV4
/** @brief the ipv4 listener socket */
static int tcp4_listen_sock = -1;
/** @brief an array of accpeted sockets */
static int tcp4_accepted[CONFIG_HTTP_NUM_HANDLERS];
#endif // CONFIG_NET_IPV4
#ifdef CONFIG_NET_IPV6
static int tcp6_listen_sock;
static int tcp6_accepted[CONFIG_HTTP_NUM_HANDLERS];
#endif

#if defined(CONFIG_NET_IPV4)
static void process_tcp4(void);
K_THREAD_DEFINE(tcp4_thread_id, CONFIG_ONBOARDING_WEB_STACK_SIZE,
		process_tcp4, NULL, NULL, NULL,
		THREAD_PRIORITY, 0, -1);
#endif

#if defined(CONFIG_NET_IPV6)
static void process_tcp6(void);
K_THREAD_DEFINE(tcp6_thread_id, CONFIG_ONBOARDING_WEB_STACK_SIZE,
		process_tcp6, NULL, NULL, NULL,
		THREAD_PRIORITY, 0, -1);
#endif

/** @brief bit mask of events to recieve */
#define EVENT_MASK (NET_EVENT_L4_CONNECTED | \
		    NET_EVENT_L4_DISCONNECTED)

/**
 * @brief handle network management events
 * called by thenetwork management subsystem
 *
 * @param cb Structure used to register the event callback
 * @param mgmt_event The network management event
 * @param iface The interface that generated the event
 */
static void ob_web_event_handler(struct net_mgmt_event_callback *cb,
			  uint64_t mgmt_event, struct net_if *iface)
{
    LOG_DBG("event 0x%llx", mgmt_event);
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}
	if (want_to_quit) {
		k_sem_give(&run_app);
        return;
	}
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("ob_web_event_handler: Network connected");
		connected = true;
		k_sem_give(&run_app);

		return;
	}
	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		if (connected == false) {
			LOG_INF("ob_web_event_handler: Waiting for network to be connected");
		} else {
			LOG_INF("ob_web_event_handler: Network disconnected");
			connected = false;
		}

        //	k_sem_reset(&run_app);

		return;
	}
}

/** @brief the maximum buffer that can be sent in a send call */
#define SENDALL_MAX_LEN 1024
/**
 * @details if the size of the buffer is larger than the maximum size for send, the routine loops, sending the maximum size on each iteration */
ssize_t sendall(int sock, const void *buf, size_t len)
{
	while (len) {
      ssize_t out_len = zsock_send(sock, buf, len>SENDALL_MAX_LEN?SENDALL_MAX_LEN: len,  0);
      LOG_DBG("Sent %d", out_len);
      if (out_len < 0) {
        LOG_ERR("send failed %d errno %d", (int) out_len,errno);
        return out_len;
      }
      buf = (const char *)buf + out_len;
      len -= out_len;
	}

	return 0;
}
/**
 * @brief create a spclet and bind the socket to an address and set the socket to listen for incomming connections.
 * @details If https is configure set the encryption keys
 *
 * @param[out] sock A pointer to the socket
 * @param bind_addr the address to bind the socket to.
 * @param bind_addrlen  The length of the bind address
 *
 * @return 0 on success
 * @return -1 on failure
 */
static int setup(int *sock, struct sockaddr *bind_addr,
		 socklen_t bind_addrlen)
{
	int ret;

#if defined(CONFIG_ONBOARDING_WEB_SERVER_HTTPS)
	*sock = zsock_socket(bind_addr->sa_family, SOCK_STREAM, IPPROTO_TLS_1_2);
#else
	*sock = zsock_socket(bind_addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
#endif
	if (*sock < 0) {
      LOG_ERR("Failed to create TCP socket: %d %d", errno, bind_addr->sa_family);
		return -1;
	}

#if defined(CONFIG_ONBOARDING_WEB_SERVER_HTTPS)

	ret = zsock_setsockopt(*sock, SOL_TLS, TLS_SEC_TAG_LIST,
			 sec_tag_list, sizeof(sec_tag_list));
	if (ret < 0) {
		LOG_ERR("Failed to set TCP secure option %d", errno);
		ret = -1;
	}
#endif // CONFIG_NET_SOCKETS_SOCKOPT

	ret = zsock_bind(*sock, bind_addr, bind_addrlen);
	if (ret < 0) {
		LOG_ERR("Failed to bind TCP socket %d", errno);
		return -1;
	}

	ret = zsock_listen(*sock, MAX_CLIENT_QUEUE);
	if (ret < 0) {
		LOG_ERR("Failed to listen on TCP socket %d", errno);
		ret = -1;
	}

	return ret;
}


/**
 * @brief types of web page operations
 */
typedef enum page_type {
  /** @brief GET opertation requested */
  GET,
  /** @brief POST operation requested */
  POST,
  /** @brief unsupported operation requested */
  UNKNOWN
} page_type_t;

/**
 * @brief send a 404 web page to the client
 *
 *@param client the socket for sending the web page
 */
static void display_404(int client)
{
  int rc;
  const char *header404 =  http1_1_404;
  rc = sendall(client, header404, strlen(header404));
  if(rc < 0) {
    LOG_ERR("HTTP 404 Header send failed %d",errno);
  }
}

/**
 * @brief send a 500 web page to the client
 *
 *@param client the socket for sending the web page
 */
static void display_500(int client)
{
  int rc;
  const char *header500 = http1_1_500;
  rc = sendall(client, header500, strlen(header500));
  if(rc < 0) {
    LOG_ERR("HTTP 500 Header send failed %d",errno);
  }
}

/**
 * @brief This function handles an incomming connection
 *
 * @param ptr1 The slot number
 * @param ptr2 This is a pointer to the socket
 * @param ptr3 This is a pointer to the thread array. It is cleared when proccessing is complete
 */
static void client_conn_handler(void *ptr1, void *ptr2, void *ptr3)
{
  ARG_UNUSED(ptr1);
  int *sock = ptr2;
  k_tid_t *in_use = ptr3;
  int client;
  int received;
  int ret;
  int i = 0;
  int rc = 0;
  int start_page;
  page_type_t page_type = UNKNOWN;
  char filename[MAX_WEB_PATH_NAME_LEN];
  char getbuf[6];
  int req_state = 0;
  client = *sock;
  web_page_t * wp;
  bool found = false;
  bool doGetLength = false;
  int length = 0;
  int ct_index = 0;

  /* Discard HTTP request (or otherwise client will get
   * connection reset error).
   */

  (void)memset(getbuf, 0, sizeof(getbuf));
  (void)memset(filename, 0, sizeof(filename));
  start_page = 0;
  i = 0;
  do {
    char c;
    received = zsock_recv(client, &c, 1, 0);
    if (received == 0) {
      /* Connection closed */
      LOG_ERR("[%d] Connection closed by peer", client);
      break;
    } else if (received < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        LOG_DBG("try again %d", errno);
        continue;
      }
      /* Socket error */
      ret = -errno;
      *sock = -1;
      LOG_ERR("[%d] Connection error %d", client, ret);
      break;
    }
#ifdef CONFIG_ONBOARDING_LOG_LEVEL_DBG
      //      putchar(c);
#endif // CONFIG_ONBOARDING_LOG_LEVEL_DBG
    if(i < 5) {
      getbuf[i] = c;
    }
    if(start_page > 0) {
      if(' ' == c) {
        filename[i-start_page] = '\0';
        start_page = 0;
        LOG_DBG("File name '%s'", filename);
      } else {
        filename[i-start_page] = c;
      }
    }
    if(i == 4) {
      if(0 == strncmp("GET ",getbuf, 4)) {
        page_type = GET;
        LOG_DBG("Found start %d", i);
        start_page = 4;
        filename[0] = c;
      }
    }
    if((!start_page) && (i == 5)) {
      if(0 == strncmp("POST", getbuf, 4)) {
        page_type = POST;
        start_page = 5;
        filename[0] = c;
      }
    }

    /* Search for Content-Length: */
    if(doGetLength) {
      if(c == '\r') {
        LOG_DBG("ContentLength %d", length);
        doGetLength =false;
      } else {
        if((c >= '0')  && ( c <= '9')) {

          length *=10;
          length += (c - '0');
        }
      }
    }
    if( content_length[ct_index] == c){
      if(CONTENT_LENGTH_END == ct_index) {
        doGetLength = true;
        ct_index = 0;
        LOG_DBG("Found content-length");
      } else {
        ct_index++;
      }
    }
    if (req_state == 0 && c == '\r') {
      req_state++;
    } else if (req_state == 1 && c == '\n') {
      req_state++;
    } else if (req_state == 2 && c == '\r') {
      req_state++;
    } else if (req_state == 3 && c == '\n') {
      break;
    } else {
      req_state = 0;
    }
    i++;

  } while (true);
  LOG_DBG("ready to process '%s'", filename);
  found = false;
  switch(page_type) {
  case GET:
    if(strlen(filename) == 1) {
      LOG_DBG("Searching for home");
      for(wp = web_pages; wp != NULL; wp = wp->next) {
        if(ob_wifi_HasAP() ) {
          if( wp->flags & PAGE_IS_CAPTIVE_PORTAL) {
            break;
          }
        } else {
          if(wp->flags & PAGE_IS_HOME_PAGE) {
            break;
          }
        }
      }
      if((NULL != wp)  && (NULL != wp->get_callback) ){
        LOG_DBG("Found %s", wp->pathname);
        rc = (*wp->get_callback)(client, wp);
        found = true;
        break;
      }
    } else {
      for(wp = web_pages; wp != NULL; wp = wp->next) {
        LOG_DBG("Checking %s", wp->pathname);
        if((0 == strncmp(wp->pathname, filename, strlen(wp->pathname))) &&
           (NULL != wp->get_callback)) {
          rc = (*wp->get_callback)(client, wp);
          found = true;
          break;
        }
      }
    }
    if(!found) {
      display_404(client);
    } else {
      if(rc < 0) {
        display_500(client);
      }
    }

    break;
  case POST:
    for(wp = web_pages; wp != NULL; wp = wp->next) {
      if(0 == strncmp(wp->pathname, filename, strlen(wp->pathname))) {
        if(NULL != wp->get_callback) {
          LOG_DBG("Posting %s", wp->pathname);
          wp->content_length = length;
          rc = (*wp->post_callback)(client, wp);
          found = true;
          break;
        }
      }
    }
    if(!found) {
      display_404(client);
    } else {
      if(rc < 0) {
        display_500(client);
      }
    }
    break;
  case UNKNOWN:
  default:
  }
  (void)zsock_close(client);
  *sock = -1;
  *in_use = NULL;
}



/*
 * ob_ws_process_post
 */
int ob_ws_process_post(int client,  post_attributes_t* ap, int num_ap, web_page_t *wp)
{
  int rc = 0;
  int i;
  int cl;
  int index;
  bool doName;
  int received;
  int value_start;
  int req_state = 0;
  char name[NAME_BUFFER_SIZE];
  char * valuebuffer;
  post_attributes_t * cp;
  cp = ap;
  //  for(i = 0; i < num_ap ; i++) {
  index = 0;
  doName = true;
  valuebuffer = cp->valuebuffer;
  LOG_DBG("Length %d", wp->content_length);
  for(cl = 0; cl < wp->content_length; cl++) {
    char c;
    received = zsock_recv(client, &c, 1, 0);
    if (received == 0) {
      /* Connection closed */
      LOG_ERR("[%d] Connection closed by peer", client);
      break;
    } else if (received < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        LOG_DBG("try again %d", errno);
        continue;
      }
      /* Socket error */
      rc = -errno;
      LOG_ERR("[%d] Connection error %d", client, rc);
      break;
    }
#ifdef CONFIG_ONBOARDING_LOG_LEVEL
    if (CONFIG_ONBOARDING_LOG_LEVEL >= LOG_LEVEL_DBG) {
      putchar(c);
    }
#endif // CONFIG_ONBOARDING_LOG_LEVEL

    if(doName) {
      if ( '=' == c) {
        value_start = index + 1;
        name[index++] = '\0';
        cp = ap;
        for(i = 0; i < num_ap ; i++) {
          LOG_DBG("Checking '%s' with '%s'", name, cp->name);
          if(!strncmp(name, cp->name, NAME_BUFFER_SIZE)) {
            LOG_DBG("Found name %s", cp->name);
            valuebuffer = cp->valuebuffer;

            break;
          }
          cp++;
        }
        if(i == num_ap) {
          LOG_ERR("Name not found");
          return -EINVAL;
        }

        doName = false;
      } else {
        if((c != '\r') && (c != '\n')) {
          if(index < NAME_BUFFER_SIZE) {
            name[index++] = c;
          } else {
            LOG_ERR("name too long %d", index);
          }
        }
      }
    } else {
      if( ('\r' == c) || ('\n' == c)) {
        valuebuffer[index - value_start] = '\0';
        LOG_DBG("VALUE = '%s'", valuebuffer);
        doName = true;
        index = 0;
      } else {
        valuebuffer[index++ - value_start] = c;
      }
    }
    if (req_state == 0 && c == '\r') {
      req_state++;
    } else if (req_state == 1 && c == '\n') {
      LOG_INF("attrib: '%s': '%s'", cp->name, cp->valuebuffer);
      //        break;
    } else {
      req_state = 0;
    }
  } ;
  putchar('\n');
  //  }
  return rc;
}

/** @brief the maximum lenght of the menu buffer */
#define WEB_MENU_LEN 256

/** @brief the start of a menu element */
static const char MENU_ELEM_START[] =  "<a href=\"";

/** @brief the end of a menu element */
static const char MENU_ELEM_END[] =  "</a>";

/** @brief the buffer for creating a menu */
static char _web_menu[WEB_MENU_LEN];

/**
 * @brief create a menu of the known web pages
 *
 * @return a pointer to a buffer containing the menu
 * @return NULL on failure
 */
static const char *
create_menu()
{
  web_page_t * wp;
  int len = 5;
  memset(_web_menu, '\0', WEB_MENU_LEN);
  strcpy(_web_menu,"<div>\n<h1 style=\"text-align:center;background-color:#3EE427;\">Beechwoods</h1>\n</div>\n<div>\n<h3 style=\"background-color:#1E90FF;\">");
  len += strlen(_web_menu);

  for(wp = web_pages; wp != NULL; wp = wp->next) {
    len += 19; //size of the constants
    len +=strlen(wp->pathname);
    len += strlen(wp->title);
    if(len > WEB_MENU_LEN) {
      LOG_ERR("Menu too big %d", len);
      return NULL;
    }
    strcat(_web_menu, "&nbsp;");
    strcat(_web_menu, MENU_ELEM_START);
    strcat(_web_menu, wp->pathname);
    strcat(_web_menu, "\">");
    strcat(_web_menu, wp->title);
    strcat(_web_menu, MENU_ELEM_END);
  }
  strcat(_web_menu, "</h3>\n</div>\n");
  LOG_DBG("Web menu:%s", _web_menu);
  return _web_menu;
}

/**
 * @details This function iterates over the registered web pages until the home page is found. @n
 * It then calls the webpage callback function to display that home page
 */
int ob_web_server_display_home(int client)
{

  int rc = 0;
  web_page_t * wp;
  bool found = false;
  LOG_DBG("Searching for home");
  for(wp = web_pages; wp != NULL; wp = wp->next) {
    if(wp->flags & PAGE_IS_HOME_PAGE) {
      if(NULL != wp->get_callback) {
        rc = (*wp->get_callback)(client, wp);
        found = true;
        break;
      }
    }
  }
  if(!found) {
    LOG_DBG("No Home Page");
    display_404(client);
    rc = -1;
  }
  return rc;
}

/**
 * @brief Find an unused slot in the array of accepted connections
 * @details IPV4 and IPV6 use separate arrays of accepted connections
 *
 * @param accepted The array of accepted connections
 */
static int get_free_slot(int *accepted)
{
	int i;

	for (i = 0; i < CONFIG_HTTP_NUM_HANDLERS; i++) {
		if (accepted[i] < 0) {
			return i;
		}
	}

	return -1;
}

/**
 * @brief Accept a connection and start a thread to process the data
 *
 * @details Accept the connection and find an open slot in the array
 * to save the accepted connection. @n
 * Start a thread toprocess the data
 *
 * @param sock a pointer to the socket to accept the connection from
 * @param accepted An array of accepted connections
 */
static int process_tcp(int *sock, int *accepted)
{
  int client;
  int slot;
  struct sockaddr_in6 client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  LOG_DBG("process tcp");
  client = zsock_accept(*sock, (struct sockaddr *)&client_addr,
                        &client_addr_len);
  if (client < 0) {
    LOG_ERR("Error in accept %d:%d, stopping server inet 0x%x", client, errno,client_addr.sin6_family);
    return -errno;
  }
  LOG_DBG("accpeted %d", client);
  slot = get_free_slot(accepted);
  if (slot < 0 || slot >= CONFIG_HTTP_NUM_HANDLERS) {
    LOG_ERR("Cannot accept more connections");
    zsock_close(client);
    return 0;
  }

  accepted[slot] = client;
#if defined(CONFIG_NET_IPV6)
  if (client_addr.sin6_family == AF_INET6) {
    tcp6_handler_tid[slot] = k_thread_create(
                                             &tcp6_handler_thread[slot],
                                             tcp6_handler_stack[slot],
                                             K_THREAD_STACK_SIZEOF(tcp6_handler_stack[slot]),
                                             &client_conn_handler,
                                             INT_TO_POINTER(slot),
                                             &accepted[slot],
                                             &tcp6_handler_tid[slot],
                                             THREAD_PRIORITY,
                                             0, K_NO_WAIT);
  }
#endif

#if defined(CONFIG_NET_IPV4)
  if (client_addr.sin6_family == AF_INET) {
    tcp4_handler_tid[slot] = k_thread_create(
                                             &tcp4_handler_thread[slot],
                                             tcp4_handler_stack[slot],
                                             K_THREAD_STACK_SIZEOF(tcp4_handler_stack[slot]),
                                             &client_conn_handler,
                                             INT_TO_POINTER(slot),
                                             &accepted[slot],
                                             &tcp4_handler_tid[slot],
                                             THREAD_PRIORITY,
                                             0, K_NO_WAIT);
  }
#endif

#ifdef CONFIG_ONBOARDING_LOG_LEVEL_DBG
  if (CONFIG_ONBOARDING_LOG_LEVEL >= LOG_LEVEL_DBG) {
    char addr_str[INET6_ADDRSTRLEN];
    static int counter;

    net_addr_ntop(client_addr.sin6_family,
 			      &client_addr.sin6_addr,
 			      addr_str, sizeof(addr_str));

    LOG_DBG("[%d] Connection #%d from %s",
 			client, ++counter,
 			addr_str);
  }
#endif // CONFIG_ONBOARDING_LOG_LEVEL
  return 0;
}

#if defined(CONFIG_NET_IPV4)
/**
 * @brief listen for an ipv4 connection
 * @details loop, accepting connections until error or quit is signalled
 */
static void process_tcp4(void)
{
  struct sockaddr_in addr4;
  int ret;

  (void)memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  addr4.sin_port = htons(MY_PORT);
  LOG_DBG("Process tcp4");
  ret = setup(&tcp4_listen_sock, (struct sockaddr *)&addr4,
              sizeof(addr4));
  if (ret < 0) {
    LOG_ERR("Setup failed %d", errno);
    return;
  }

  LOG_DBG("Waiting for IPv4 HTTP connections on port %d, sock %d",
          MY_PORT, tcp4_listen_sock);

  while (ret == 0 || !want_to_quit) {
    ret = process_tcp(&tcp4_listen_sock, tcp4_accepted);
    if (ret < 0) {
      LOG_ERR("Proccess tcp failed %d", errno);
    }
  }
  if(tcp4_listen_sock >= 0 ) {
    zsock_close(tcp4_listen_sock);
    tcp4_listen_sock = -1;
  }
}
#endif

#if defined(CONFIG_NET_IPV6)

static void process_tcp6(void)
{
	struct sockaddr_in6 addr6;
	int ret;

	(void)memset(&addr6, 0, sizeof(addr6));
	addr6.sin6_family = AF_INET6;
	addr6.sin6_port = htons(MY_PORT);

	ret = setup(&tcp6_listen_sock, (struct sockaddr *)&addr6,
		    sizeof(addr6));
	if (ret < 0) {
		return;
	}

	LOG_DBG("Waiting for IPv6 HTTP connections on port %d, sock %d",
		MY_PORT, tcp6_listen_sock);

	while (ret == 0 || !want_to_quit) {
		ret = process_tcp(&tcp6_listen_sock, tcp6_accepted);
		if (ret != 0) {
			return;
		}
	}
}
#endif
/**
 * @brief start the listener thread(s)
 * @details The IPV4 and/or the IPV6 listener threads are started
 */
static void start_listener(void)
{
	int i;

	for (i = 0; i < CONFIG_HTTP_NUM_HANDLERS; i++) {
#ifdef CONFIG_NET_IPV4
		tcp4_accepted[i] = -1;
#endif
#ifdef CONFIG_NET_IPV6
		tcp6_accepted[i] = -1;
#endif
	}

#ifdef CONFIG_NET_IPV6
    tcp6_listen_sock = -1;
	if (IS_ENABLED(CONFIG_NET_IPV6)) {
		k_thread_start(tcp6_thread_id);
	}
#endif
#ifdef CONFIG_NET_IPV4
	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		k_thread_start(tcp4_thread_id);
	}
#endif
}

int stop_web_server(void)
{

  LOG_DBG("Stop web server");
  want_to_quit = true;
#if defined(CONFIG_NET_IPV6)
  k_thread_abort(tcp6_thread_id);
#endif
#if defined(CONFIG_NET_IPV4)
  k_thread_abort(tcp4_thread_id);
#endif
  mWebServerRunning = false;
  return 0;
}

/**
 * @brief implementation for starting the web server
 */
static void do_start_web_server(struct k_work * work)
{

    int rc;
    LOG_DBG("Start web server");
    if(mWebServerRunning) {
      return;
    }
    mWebServerRunning = true;
    want_to_quit = false;
	if (!IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		/* If the config library has not been configured to start the
		 * app only after we have a connection, then we can start
		 * it right away.
		 */
        LOG_DBG("sem_give runapp");
		k_sem_give(&run_app);
	}

	/* Wait for the connection. */
    rc = k_sem_take(&run_app, K_FOREVER);
    if(rc < 0) {
      LOG_ERR("Start web server could not obtain semaphore");
    } else {
      start_listener();
    }
    //	k_sem_take(&quit_lock, K_FOREVER);

    LOG_WRN("Leaving web server");
}


int
init_web_server(void)
{
    int err;
    LOG_DBG("init web server");
    err = ob_nvs_data_init();
    if(err < 0) {
      return err;
    }
    k_work_init(&start_web_server_work, do_start_web_server);
#if defined(CONFIG_ONBOARDING_WEB_SERVER_HTTPS)
    err = tls_credential_add(sec_tag_list[0],
                             TLS_CREDENTIAL_CA_CERTIFICATE,
                             ob_cert_get(CA_CERT),
                             ob_cert_len(CA_CERT));
	if (err < 0) {
		LOG_ERR("Failed to register ca certificate: %d", err);
	}
	err = tls_credential_add(sec_tag_list[0],
                             TLS_CREDENTIAL_SERVER_CERTIFICATE,
                             ob_cert_get(PUBLIC_CERT),
                             ob_cert_len(PUBLIC_CERT));
	if (err < 0) {
		LOG_ERR("Failed to register public certificate: %d", err);
	}
    //    private_key[sizeof(private_key) -1] = 0;
    LOG_DBG("Set Private key");
	err = tls_credential_add(sec_tag_list[0],
                             TLS_CREDENTIAL_PRIVATE_KEY,
                             ob_cert_get(PRIVATE_KEY),
                             ob_cert_len(PRIVATE_KEY));
	if (err < 0) {
		LOG_ERR("Failed to register private key: %d", err);
	}
#endif

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		net_mgmt_init_event_callback(&mgmt_cb,
					     ob_web_event_handler, EVENT_MASK);
		net_mgmt_add_event_callback(&mgmt_cb);

		conn_mgr_mon_resend_status();
	}
    web_server_inited =true;
    return err;

}

/** @details the web server runs on the system work thread */
void start_web_server(void)
{
  if(!web_server_inited) {
    init_web_server();
  }
  k_work_submit(&start_web_server_work);
}

/** @brief size of the static http OK buffer */
#define HEADER200_SIZE 1024

/** @brief the static http OK buffer */
static char Header200[HEADER200_SIZE];

/**
 * @details Creates the HTTP 200 OK header with a menu
 *
 */
char * CreateHeader200(int contentlen, const char * title)
{
  char itembuf[MAX_HEADER_CONTENT_LEN];
  char *retval = NULL;
  int len;
  const char * menu;
  menu = create_menu();
  if(NULL != menu) {
    contentlen += strlen(content_head) + strlen(title) + strlen(content_head_tail) + strlen(menu);
    LOG_DBG("%d %d %d %d", strlen(content_head) , strlen(title) , strlen(content_head_tail) , strlen(menu));
    snprintf(itembuf, MAX_HEADER_CONTENT_LEN, content_length, contentlen);
    len = strlen(http1_1_OK) + strlen(itembuf) + strlen(content_head) + sizeof(CONTENT_TYPE);
    len += strlen(menu);
    len++;
    if(len < HEADER200_SIZE) {
      retval = Header200;
      strcpy(retval, http1_1_OK);
      strcat(retval,itembuf);
      strcat(retval, CONTENT_TYPE);
      strcat(retval, content_head);
      strcat(retval, title);
      strcat(retval, content_head_tail);
      strcat(retval, menu);
    } else {
      LOG_ERR("Requested size %d greater than available buffer %d", len, HEADER200_SIZE);
    }
  } else {
    LOG_ERR("create_menu - nomem");
    retval = NULL;
  }
  return retval;
}

int ob_ws_register_web_page(const char * pathname, const char * title, ob_web_display_page get_callback,ob_web_display_page post_callback, int flags) {
  web_page_t * wp;
  bool found = false;
  wp = web_pages;
  while(NULL != wp) {
    if(!strncmp(pathname, wp->pathname, MAX_WEB_PATH_NAME_LEN)) {
      found = true;
      break;
    }
    wp = wp->next;
  }
  if(!found) {
    LOG_DBG("Adding web page %s",pathname);
    wp = malloc(sizeof(web_page_t));
    if(NULL != wp) {
      strncpy(wp->pathname, pathname, MAX_WEB_PATH_NAME_LEN);
      strncpy(wp->title, title, MAX_WEB_TITLE_LEN);
      wp->flags = flags;
      wp->content_length = 0;
      wp->get_callback = get_callback;
      wp->post_callback = post_callback;
      wp->next = web_pages;
      web_pages = wp;
    } else {
      LOG_ERR("Unable to allocate web page");
      return -1;
    }
  } else {
    LOG_DBG("Web page %s already exists", pathname);
  }
  return 0;
}


#endif // CONFIG_ONBOARDING_WEB_SERVER

