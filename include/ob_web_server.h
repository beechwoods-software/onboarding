/*
 * Copyright(C) 2024 Brad Kemp Beechwoods Software, Inc.
 * All Rights Reserved
 * SPDX-License-Identifier: Apache 2.0
 */
#pragma once
#include <sys/types.h>

/**
 * @brief The maximum sizeof a web path name
 */
#define MAX_WEB_PATH_NAME_LEN 32

/**
 * @brief The maximum size of a web page title
 */
#define MAX_WEB_TITLE_LEN 32
struct web_page;
/**
 * @brief a typedef of a function pointer for displaying web pages
 *
 * @param client - the socket used in sending the page
 * @param wp - a pointer to the web page
 *
 * @returns 0 on success
 * @returns -1 on error
 */
typedef int (*ob_web_display_page)(int client, struct web_page * wp );

/**  @brief page to display when / is requested and not in captive portal mode */
#define PAGE_IS_HOME_PAGE      0x01

/** @brief Page to display when / is requested and in captive portal mode. */
#define PAGE_IS_CAPTIVE_PORTAL 0x02

/**
 * @struct web_page
 * @brief an element in a linked list of web pages
 */
struct web_page {
  /**
   * @var struct web_page * next
   * @brief pointer to next web page, NULL if tail
   */
  struct web_page *next;
  /**
   * @var char pathname[MAX_WEB_PATH_NAME_LEN]
   * @brief the path name for the web page
   */
  char pathname[MAX_WEB_PATH_NAME_LEN];
  /**
   * @var char title[MAX_WEB_TITLE_LEN]
   * @brief the title of the web page
   */
  char title[MAX_WEB_TITLE_LEN];
  /**
   * @var int flags
   * @brief flags for the web page
   */
  int flags;
  /**
   * @var int content_length
   * @brief the length of the web page
   */
  int content_length;
  /**
   * @var ob_web_display_page get_callback
   * @brief a callback to display a web page on a GET on this page
   */
  ob_web_display_page get_callback;
  /**
   * @var ob_web_display_page post_callback
   * @brief a callback to process a POST on this page
   */
  ob_web_display_page post_callback;
};
/**
 * @brief This typedef provides an alias for the struct web_page
 */
typedef struct web_page web_page_t;

/**
 * @brief the maximum size of an attribute value
 */
#define NAME_BUFFER_SIZE 32

/**
 * @brief the maximum size of an attribute value
 */
#define VALUE_BUFFER_SIZE 65
/**
 * @struct post_attributes
 *
 * @brief This structure contains the name of the attribute, its length and a buffer to hold the value of the attribute
 */
struct post_attributes {
  /** @brief The name of the attribute */
  const char * name;
  /** @brief The length of the attribute name */
  int length;
  /** @brief the buffer to hold the attribute value */
  char valuebuffer[VALUE_BUFFER_SIZE];
};

/**
 * @brief This typedef provides an alias for struct post_attributes
 */
typedef struct post_attributes post_attributes_t;

/**
 * @struct _value_action
 * @brief actions for attributes from a post
 * This structure contains data for the application to perform actions based on
 * the data return from a post.
 */
struct _value_action {
  /**
   * @var const char * match
   * @brief the value to be checked
   */
  const char * match;
  /**
   * @var int length
   * @brief the length of the match name to be compared
   */
  int length;
  /**
   * @var void * userdata
   * @brief pointer to application data
   */
  void * userdata;
};
/**
 * @brief This typedef provides an alias to the struct _value_action
 */
typedef struct _value_action value_action_t;

/**
 * @brief initialize the web server
 * This function intializes nvram and if TLS is enabled adds the web server
 * certificates to the web server
 * @return 0 on success
 * @return -1 on failure
 * 
 **/
int init_web_server(void);

/**
 * @brief add a web page to the web server
 *
 * This all will add a web page to the web server. It will also insert the path
 * of the page into the menu of all web pages with tile as its list item text.
 * The PUT command is not supported
 *
 * @param pathname The pathname for the web page
 * @param title The title for the web page and the text for the menu item
 *          referencing this page
 * @param get_callback Pointer to funcdtion to call when the page is accessed
 *        by a GET
 * @param post_callback Pointer to function to call when the page is accessed
 *        by a POST
 * @param flags Mark this page as the home page or captive portal page
 *
 * @returns 0 on success -1 on failure
 **/
int ob_ws_register_web_page(const char * pathname, const char * title,
                            ob_web_display_page get_callback,
                            ob_web_display_page post_callback,
                            int flags);

/**
 * @brief start the web server
 */
void start_web_server(void);

/**
 * @brief stop the web server
 */
int stop_web_server(void);

/**
 * @brief send a buffer over a socket
 *
 * @param sock the socket file descriptor
 * @param buf the buffer hoding the data to send
 * @param len the lenght of the buffer
 * @return the number of bytes sent on success
 * @return -1 on failure
 */
ssize_t sendall(int sock, const void *buf, size_t len);

/**
 * @brief ob_ws_process_post - fill in values from a POST request
 *
 * This function matches the attributes passed in the post_attibutes_t array
 * with attributes from the POST request. When a match is found the
 * corrosponding valuebuffer is filled in.
 *
 * @param client The socket for the client
 * @param ap A pointer to an array of post_attribute_t structures
 * @param num_ap The number of elements in the ap array.
 * @param wp a pointer to the web_page_t instance for this web page
 *
 */
int ob_ws_process_post(int client,  post_attributes_t* ap, int num_ap, web_page_t * wp);
/**
 * @brief ob_web_server_display_home - display the web servers home page
 *
 * @param client - socket descriptor for client
 **/
int ob_web_server_display_home(int client);

/**
 * @brief send part of a web page to a client
 *
 * @param fd  the socket to send the data on
 * @param slice pointer to the data. The data us assumed to be a null terminated character string
 */
#define SEND_SLICE(fd, slice) { if(sendall(fd, slice, strlen(slice)) < 0 ) { LOG_ERR("HTTP " #slice  " send failed %d", errno); }}

/**
 * @brief Return a buffer for a HTTP 200 response
 *
 * @param contentlen The length of the web page
 * @param title The title of the web page
 *
 * @return a pointer to the buffer containing the header
 * @return NULL on failure
 */
char * CreateHeader200(int contentlen, const char * title);

