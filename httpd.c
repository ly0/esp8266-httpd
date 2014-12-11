/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

/* This httpd supports for a
 * rudimentary server-side-include facility which will replace tags of the form
 * <!--#tag--> in any file whose extension is .shtml, .shtm or .ssi with
 * strings provided by an include handler whose pointer is provided to the
 * module via function http_set_ssi_handler().
 * Additionally, a simple common
 * gateway interface (CGI) handling mechanism has been added to allow clients
 * to hook functions to particular request URIs.
 *
 * To enable SSI support, define label LWIP_HTTPD_SSI in lwipopts.h.
 * To enable CGI support, define label LWIP_HTTPD_CGI in lwipopts.h.
 *
 * By default, the server assumes that HTTP headers are already present in
 * each file stored in the file system.  By defining LWIP_HTTPD_DYNAMIC_HEADERS in
 * lwipopts.h, this behavior can be changed such that the server inserts the
 * headers automatically based on the extension of the file being served.  If
 * this mode is used, be careful to ensure that the file system image used
 * does not already contain the header information.
 *
 * File system images without headers can be created using the makefsfile
 * tool with the -h command line option.
 *
 *
 * Notes about valid SSI tags
 * --------------------------
 *
 * The following assumptions are made about tags used in SSI markers:
 *
 * 1. No tag may contain '-' or whitespace characters within the tag name.
 * 2. Whitespace is allowed between the tag leadin "<!--#" and the start of
 *    the tag name and between the tag name and the leadout string "-->".
 * 3. The maximum tag name length is LWIP_HTTPD_MAX_TAG_NAME_LEN, currently 8 characters.
 *
 * Notes on CGI usage
 * ------------------
 *
 * The simple CGI support offered here works with GET method requests only
 * and can handle up to 16 parameters encoded into the URI. The handler
 * function may not write directly to the HTTP output but must return a
 * filename that the HTTP server will send to the browser as a response to
 * the incoming CGI request.
 *
 * @todo:
 * - don't use mem_malloc() (for SSI/dynamic headers)
 * - split too long functions into multiple smaller functions?
 * - support more file types?
 */
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "httpd.h"
#include "httpd_structs.h"
#include "lwip/tcp.h"
#include "fs.h"
#include "api_struct.h"
#include "http_request.h"

#include <string.h>
#include <stdlib.h>

#if LWIP_TCP

#ifndef HTTPD_DEBUG
#define HTTPD_DEBUG         LWIP_DBG_OFF
#endif

/** Set this to 1 and add the next line to lwippools.h to use a memp pool
 * for allocating struct http_state instead of the heap:
 *
 * LWIP_MEMPOOL(HTTPD_STATE, 20, 100, "HTTPD_STATE")
 */
#ifndef HTTPD_USE_MEM_POOL
#define HTTPD_USE_MEM_POOL  0
#endif

/** The server port for HTTPD to use */
#ifndef HTTPD_SERVER_PORT
#define HTTPD_SERVER_PORT                   80
#endif

/** Maximum retries before the connection is aborted/closed.
 * - number of times pcb->poll is called -> default is 4*500ms = 2s;
 * - reset when pcb->sent is called
 */
#ifndef HTTPD_MAX_RETRIES
#define HTTPD_MAX_RETRIES                   4
#endif

/** The poll delay is X*500ms */
#ifndef HTTPD_POLL_INTERVAL
#define HTTPD_POLL_INTERVAL                 4
#endif

/** Priority for tcp pcbs created by HTTPD (very low by default).
 *  Lower priorities get killed first when running out of memroy.
 */
#ifndef HTTPD_TCP_PRIO
#define HTTPD_TCP_PRIO                      TCP_PRIO_MIN
#endif

/** Set this to 1 to enabled timing each file sent */
#ifndef LWIP_HTTPD_TIMING
#define LWIP_HTTPD_TIMING                   0
#endif
#ifndef HTTPD_DEBUG_TIMING
#define HTTPD_DEBUG_TIMING                  LWIP_DBG_OFF
#endif

/** Set this to 1 on platforms where strnstr is not available */
#ifndef LWIP_HTTPD_STRNSTR_PRIVATE
#define LWIP_HTTPD_STRNSTR_PRIVATE          1
#endif

/** Set this to one to show error pages when parsing a request fails instead
    of simply closing the connection. */
#ifndef LWIP_HTTPD_SUPPORT_EXTSTATUS
#define LWIP_HTTPD_SUPPORT_EXTSTATUS        0
#endif

/** Set this to 0 to drop support for HTTP/0.9 clients (to save some bytes) */
#ifndef LWIP_HTTPD_SUPPORT_V09
#define LWIP_HTTPD_SUPPORT_V09              1
#endif

/** Set this to 1 to support HTTP request coming in in multiple packets/pbufs */
#ifndef LWIP_HTTPD_SUPPORT_REQUESTLIST
#define LWIP_HTTPD_SUPPORT_REQUESTLIST      0
#endif

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
/** Number of rx pbufs to enqueue to parse an incoming request (up to the first
    newline) */
#ifndef LWIP_HTTPD_REQ_QUEUELEN
#define LWIP_HTTPD_REQ_QUEUELEN             10
#endif

/** Number of (TCP payload-) bytes (in pbufs) to enqueue to parse and incoming
    request (up to the first double-newline) */
#ifndef LWIP_HTTPD_REQ_BUFSIZE
#define LWIP_HTTPD_REQ_BUFSIZE              LWIP_HTTPD_MAX_REQ_LENGTH
#endif

/** Defines the maximum length of a HTTP request line (up to the first CRLF,
    copied from pbuf into this a global buffer when pbuf- or packet-queues
    are received - otherwise the input pbuf is used directly) */
#ifndef LWIP_HTTPD_MAX_REQ_LENGTH
#define LWIP_HTTPD_MAX_REQ_LENGTH           1023
#endif
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */

/** Maximum length of the filename to send as response to a POST request,
 * filled in by the application when a POST is finished.
 */
#ifndef LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN
#define LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN 63
#endif

/** Set this to 0 to not send the SSI tag (default is on, so the tag will
 * be sent in the HTML page */
#ifndef LWIP_HTTPD_SSI_INCLUDE_TAG
#define LWIP_HTTPD_SSI_INCLUDE_TAG           1
#endif

/** Set this to 1 to call tcp_abort when tcp_close fails with memory error.
 * This can be used to prevent consuming all memory in situations where the
 * HTTP server has low priority compared to other communication. */
#ifndef LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR
#define LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR  0
#endif

#ifndef true
#define true ((u8_t)1)
#endif

#ifndef false
#define false ((u8_t)0)
#endif

/** Minimum length for a valid HTTP/0.9 request: "GET /\r\n" -> 7 bytes */
#define MIN_REQ_LEN   7

#define CRLF "\r\n"

/** These defines check whether tcp_write has to copy data or not */

/** This was TI's check whether to let TCP copy data or not
#define HTTP_IS_DATA_VOLATILE(hs) ((hs->file < (char *)0x20000000) ? 0 : TCP_WRITE_FLAG_COPY)*/
#ifndef HTTP_IS_DATA_VOLATILE
#if LWIP_HTTPD_SSI
/* Copy for SSI files, no copy for non-SSI files */
#define HTTP_IS_DATA_VOLATILE(hs)   ((hs)->tag_check ? TCP_WRITE_FLAG_COPY : 0)
#else /* LWIP_HTTPD_SSI */
/** Default: don't copy if the data is sent from file-system directly */
#define HTTP_IS_DATA_VOLATILE(hs) (((hs->file != NULL) && (hs->handle != NULL) && (hs->file == \
                                   (char*)hs->handle->data + hs->handle->len - hs->left)) \
                                   ? 0 : TCP_WRITE_FLAG_COPY)
#endif /* LWIP_HTTPD_SSI */
#endif

/** Default: headers are sent from ROM */
#ifndef HTTP_IS_HDR_VOLATILE
#define HTTP_IS_HDR_VOLATILE(hs, ptr) 0
#endif

#if LWIP_HTTPD_SSI
/** Default: Tags are sent from struct http_state and are therefore volatile */
#ifndef HTTP_IS_TAG_VOLATILE
#define HTTP_IS_TAG_VOLATILE(ptr) TCP_WRITE_FLAG_COPY
#endif
#endif /* LWIP_HTTPD_SSI */

typedef struct
{
  const char *name;
  u8_t shtml;
} default_filename;

const default_filename g_psDefaultFilenames[] = {
  {"/", true }
};

#define NUM_DEFAULT_FILENAMES (sizeof(g_psDefaultFilenames) /   \
                               sizeof(default_filename))

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
/** HTTP request is copied here from pbufs for simple parsing */
static char httpd_req_buf[LWIP_HTTPD_MAX_REQ_LENGTH+1];
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */

/** Filename for response file to send when POST is finished */
static char http_post_response_filename[LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN+1];

/* The number of individual strings that comprise the headers sent before each
 * requested file.
 */
#define NUM_FILE_HDR_STRINGS 3


struct http_state {
  struct webfs_file *handle;
  char *file;       /* Pointer to first unsent byte in buf. */

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
  struct pbuf *req;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */

#if LWIP_HTTPD_SSI || LWIP_HTTPD_DYNAMIC_HEADERS
  char *buf;        /* File read buffer. */
  int buf_len;      /* Size of file read buffer, buf. */
#endif /* LWIP_HTTPD_SSI || LWIP_HTTPD_DYNAMIC_HEADERS */
  u32_t left;       /* Number of unsent bytes in buf. */
  u8_t retries;
#if LWIP_HTTPD_SSI
  const char *parsed;     /* Pointer to the first unparsed byte in buf. */
#if !LWIP_HTTPD_SSI_INCLUDE_TAG
  const char *tag_started;/* Poitner to the first opening '<' of the tag. */
#endif /* !LWIP_HTTPD_SSI_INCLUDE_TAG */
  const char *tag_end;    /* Pointer to char after the closing '>' of the tag. */
  u32_t parse_left; /* Number of unparsed bytes in buf. */
  u16_t tag_index;   /* Counter used by tag parsing state machine */
  u16_t tag_insert_len; /* Length of insert in string tag_insert */
#if LWIP_HTTPD_SSI_MULTIPART
  u16_t tag_part; /* Counter passed to and changed by tag insertion function to insert multiple times */
#endif /* LWIP_HTTPD_SSI_MULTIPART */
  u8_t tag_check;   /* true if we are processing a .shtml file else false */
  u8_t tag_name_len; /* Length of the tag name in string tag_name */
  char tag_name[LWIP_HTTPD_MAX_TAG_NAME_LEN + 1]; /* Last tag name extracted */
  char tag_insert[LWIP_HTTPD_MAX_TAG_INSERT_LEN + 1]; /* Insert string for tag_name */
  enum tag_check_state tag_state; /* State of the tag processor */
#endif /* LWIP_HTTPD_SSI */
  char *params[LWIP_HTTPD_MAX_CGI_PARAMETERS]; /* Params extracted from the request URI */
  char *param_vals[LWIP_HTTPD_MAX_CGI_PARAMETERS]; /* Values for each extracted param */
#if LWIP_HTTPD_DYNAMIC_HEADERS
  const char *hdrs[NUM_FILE_HDR_STRINGS]; /* HTTP headers to be sent. */
  u16_t hdr_pos;     /* The position of the first unsent header byte in the
                        current string */
  u16_t hdr_index;   /* The index of the hdr string currently being sent. */
#endif /* LWIP_HTTPD_DYNAMIC_HEADERS */
#if LWIP_HTTPD_TIMING
  u32_t time_started;
#endif /* LWIP_HTTPD_TIMING */
#if LWIP_HTTPD_SUPPORT_POST
  u32_t post_content_len_left;
#if LWIP_HTTPD_POST_MANUAL_WND
  u32_t unrecved_bytes;
  struct tcp_pcb *pcb;
  u8_t no_auto_wnd;
#endif /* LWIP_HTTPD_POST_MANUAL_WND */
  /* HTTP POST FIELD*/
  HTTPRequest req_info;
#endif /* LWIP_HTTPD_SUPPORT_POST*/
};

static err_t http_find_file(struct http_state *hs, const char *uri, int is_09);
static err_t http_init_file(struct http_state *hs, struct webfs_file *file, int is_09, const char *uri);
static err_t http_poll(void *arg, struct tcp_pcb *pcb);

#if LWIP_HTTPD_SSI
/* SSI insert handler function pointer. */
tSSIHandler g_pfnSSIHandler = NULL;
int g_iNumTags = 0;
const char **g_ppcTags = NULL;

#define LEN_TAG_LEAD_IN 5
const char * const g_pcTagLeadIn = "<!--#";

#define LEN_TAG_LEAD_OUT 3
const char * const g_pcTagLeadOut = "-->";
#endif /* LWIP_HTTPD_SSI */

#if LWIP_HTTPD_CGI
/* CGI handler information */
const tCGI *g_pCGIs = NULL;
int g_iNumCGIs = 0;
#endif /* LWIP_HTTPD_CGI */

#if LWIP_HTTPD_STRNSTR_PRIVATE
/** Like strstr but does not need 'buffer' to be NULL-terminated */
static char* ICACHE_FLASH_ATTR
strnstr(const char* buffer, const char* token, size_t n)
{
  const char* p;
  int tokenlen = (int)strlen(token);
  if (tokenlen == 0) {
    return (char *)buffer;
  }
  for (p = buffer; *p && (p + tokenlen <= buffer + n); p++) {
    if ((*p == *token) && (strncmp(p, token, tokenlen) == 0)) {
      return (char *)p;
    }
  }
  return NULL;
} 
#endif /* LWIP_HTTPD_STRNSTR_PRIVATE */

/** Allocate a struct http_state. */
static struct http_state* ICACHE_FLASH_ATTR
http_state_alloc(void)
{
  struct http_state *ret;
#if HTTPD_USE_MEM_POOL
  ret = (struct http_state *)memp_malloc(MEMP_HTTPD_STATE);
#else /* HTTPD_USE_MEM_POOL */
  ret = (struct http_state *)mem_malloc(sizeof(struct http_state));
#endif /* HTTPD_USE_MEM_POOL */
  if (ret != NULL) {
    /* Initialize the structure. */
    memset(ret, 0, sizeof(struct http_state));
#if LWIP_HTTPD_DYNAMIC_HEADERS
    /* Indicate that the headers are not yet valid */
    ret->hdr_index = NUM_FILE_HDR_STRINGS;
#endif /* LWIP_HTTPD_DYNAMIC_HEADERS */
  }
  return ret;
}

/** Free a struct http_state.
 * Also frees the file data if dynamic.
 */
static void ICACHE_FLASH_ATTR
http_state_free(struct http_state *hs)
{
  if (hs != NULL) {
    if(hs->handle) {
#if LWIP_HTTPD_TIMING
      u32_t ms_needed = sys_now() - hs->time_started;
      u32_t needed = LWIP_MAX(1, (ms_needed/100));
      LWIP_DEBUGF(HTTPD_DEBUG_TIMING, ("httpd: needed %"U32_F" ms to send file of %d bytes -> %"U32_F" bytes/sec\n",
        ms_needed, hs->handle->len, ((((u32_t)hs->handle->len) * 10) / needed)));
#endif /* LWIP_HTTPD_TIMING */
      printf("[*] FREE hs->file \n");
      if (hs->handle->data)
          free((char*)hs->handle->data);
      webfs_close(hs->handle);
      hs->handle = NULL;
    }
#if LWIP_HTTPD_SSI || LWIP_HTTPD_DYNAMIC_HEADERS
    if (hs->buf != NULL) {
      mem_free(hs->buf);
      hs->buf = NULL;
    }
#endif /* LWIP_HTTPD_SSI || LWIP_HTTPD_DYNAMIC_HEADERS */
#if HTTPD_USE_MEM_POOL
    memp_free(MEMP_HTTPD_STATE, hs);
#else /* HTTPD_USE_MEM_POOL */
    mem_free(hs);
#endif /* HTTPD_USE_MEM_POOL */
  }
}

/** Call tcp_write() in a loop trying smaller and smaller length
 *
 * @param pcb tcp_pcb to send
 * @param ptr Data to send
 * @param length Length of data to send (in/out: on return, contains the
 *        amount of data sent)
 * @param apiflags directly passed to tcp_write
 * @return the return value of tcp_write
 */
static err_t ICACHE_FLASH_ATTR
http_write(struct tcp_pcb *pcb, const void* ptr, u16_t *length, u8_t apiflags)
{
   printf("[*] http_write invoked, content=%s\n", ptr);
   u16_t len;
   err_t err;
   LWIP_ASSERT("length != NULL", length != NULL);
   len = *length;
   do {
     LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Trying to send %d bytes\n", len));
     printf("WRITE %d bytes, content: %s", len,ptr);
     err = tcp_write(pcb, ptr, len, apiflags);
     if (err == ERR_MEM) {
       if ((tcp_sndbuf(pcb) == 0) ||
           (tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN)) {
         /* no need to try smaller sizes */
         len = 1;
       } else {
         len /= 2;
       }
       LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, 
                   ("Send failed, trying less (%d bytes)\n", len));
     }
   } while ((err == ERR_MEM) && (len > 1));
   if (err == ERR_OK) {
     LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Sent %d bytes\n", len));
   } else {
     LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Send failed with err %d (\"%s\")\n", err, lwip_strerr(err)));
   }

   *length = len;
   return err;
}

/**
 * The connection shall be actively closed.
 * Reset the sent- and recv-callbacks.
 *
 * @param pcb the tcp pcb to reset callbacks
 * @param hs connection state to free
 */
static err_t ICACHE_FLASH_ATTR
http_close_conn(struct tcp_pcb *pcb, struct http_state *hs)
{
  err_t err;
  LWIP_DEBUGF(HTTPD_DEBUG, ("Closing connection %p\n", (void*)pcb));

#if LWIP_HTTPD_SUPPORT_POST
  if (hs != NULL) {
    if ((hs->post_content_len_left != 0)
#if LWIP_HTTPD_POST_MANUAL_WND
       || ((hs->no_auto_wnd != 0) && (hs->unrecved_bytes != 0))
#endif /* LWIP_HTTPD_POST_MANUAL_WND */
       ) {
      /* make sure the post code knows that the connection is closed */
      http_post_response_filename[0] = 0;
      httpd_post_finished(hs, http_post_response_filename, LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN);
    }
  }
#endif /* LWIP_HTTPD_SUPPORT_POST*/


  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_poll(pcb, NULL, 0);
  tcp_sent(pcb, NULL);
  if(hs != NULL) {
    http_state_free(hs);
  }

  err = tcp_close(pcb);
  if (err != ERR_OK) {
    LWIP_DEBUGF(HTTPD_DEBUG, ("Error %d closing %p\n", err, (void*)pcb));
    /* error closing, try again later in poll */
    tcp_poll(pcb, http_poll, HTTPD_POLL_INTERVAL);
  }
  return err;
}

/**
 * Generate the relevant HTTP headers for the given filename and write
 * them into the supplied buffer.
 */
static void ICACHE_FLASH_ATTR
get_http_headers(struct http_state *pState, char *pszURI)
{
  unsigned int iLoop;
  char *pszWork;
  char *pszExt;
  char *pszVars;

  /* Ensure that we initialize the loop counter. */
  iLoop = 0;

  /* In all cases, the second header we send is the server identification
     so set it here. */
  pState->hdrs[1] = g_psHTTPHeaderStrings[HTTP_HDR_SERVER];

  /* Is this a normal file or the special case we use to send back the
     default "404: Page not found" response? */
  if (pszURI == NULL) {
    pState->hdrs[0] = g_psHTTPHeaderStrings[HTTP_HDR_NOT_FOUND];
    pState->hdrs[2] = g_psHTTPHeaderStrings[DEFAULT_404_HTML];

    /* Set up to send the first header string. */
    pState->hdr_index = 0;
    pState->hdr_pos = 0;
    return;
  } else {
    /* We are dealing with a particular filename. Look for one other
       special case.  We assume that any filename with "404" in it must be
       indicative of a 404 server error whereas all other files require
       the 200 OK header. */
    if (strstr(pszURI, "404")) {
      pState->hdrs[0] = g_psHTTPHeaderStrings[HTTP_HDR_NOT_FOUND];
    } else if (strstr(pszURI, "400")) {
      pState->hdrs[0] = g_psHTTPHeaderStrings[HTTP_HDR_BAD_REQUEST];
    } else if (strstr(pszURI, "501")) {
      pState->hdrs[0] = g_psHTTPHeaderStrings[HTTP_HDR_NOT_IMPL];
    } else {
      pState->hdrs[0] = g_psHTTPHeaderStrings[HTTP_HDR_OK];
    }

    /* Determine if the URI has any variables and, if so, temporarily remove 
       them. */
    pszVars = strchr(pszURI, '?');
    if(pszVars) {
      *pszVars = '\0';
    }

    /* Get a pointer to the file extension.  We find this by looking for the
       last occurrence of "." in the filename passed. */
    pszExt = NULL;
    pszWork = strchr(pszURI, '.');
    while(pszWork) {
      pszExt = pszWork + 1;
      pszWork = strchr(pszExt, '.');
    }

    if (pszExt != NULL)
    {
        /* Now determine the content type and add the relevant header for that. */
        for(iLoop = 0; (iLoop < NUM_HTTP_HEADERS) && pszExt; iLoop++) {
          /* Have we found a matching extension? */
          if(!strcmp(g_psHTTPHeaders[iLoop].extension, pszExt)) {
            pState->hdrs[2] =
              g_psHTTPHeaderStrings[g_psHTTPHeaders[iLoop].headerIndex];
            break;
          }
        }
    } else {
        pState->hdrs[2] = g_psHTTPHeaderStrings[HTTP_HDR_JSON];
    }

    /* Reinstate the parameter marker if there was one in the original URI. */
    if(pszVars) {
      *pszVars = '?';
    }
  }

  pState->hdr_index = 0;
  pState->hdr_pos = 0;
  printf("[*] HEADER \n %s \n %s \n %s \n", pState->hdrs[0], pState->hdrs[1] ,pState->hdrs[2]);
}


/**
 * Try to send more data on this pcb.
 *
 * @param pcb the pcb to send data
 * @param hs connection state
 */
static u8_t ICACHE_FLASH_ATTR
http_send_data(struct tcp_pcb *pcb, struct http_state *hs)
{
  printf("[*] http_send_data invoked\n");
  err_t err;
  u16_t len;
  u16_t mss;
  u8_t data_to_send = false;
  u16_t hdrlen, sendlen;

  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("http_send_data: pcb=%p hs=%p left=%d\n", (void*)pcb,
    (void*)hs, hs != NULL ? hs->left : 0));

  /* Send header*/
  printf("[*] LWIP_HTTPD_DYNAMIC_HEADERS on\n");
  /* If we were passed a NULL state structure pointer, ignore the call. */
  if (hs == NULL) {
    return 0;
  }

  /* Assume no error until we find otherwise */
  err = ERR_OK;
  printf("hs->hdr_index=%d, NUM_FILE_HDR_STRINGS=%d", hs->hdr_index, NUM_FILE_HDR_STRINGS);
  /* Do we have any more header data to send for this file? */
  if(hs->hdr_index < NUM_FILE_HDR_STRINGS) {
    /* How much data can we send? */
    len = tcp_sndbuf(pcb);
    sendlen = len;
    printf("[*] http_send_data after tcp_sndbuf, len=%d, sendlen=%d\n", len, sendlen);

    while(len && (hs->hdr_index < NUM_FILE_HDR_STRINGS) && sendlen) {
      const void *ptr;
      u16_t old_sendlen;
      /* How much do we have to send from the current header? */
      hdrlen = (u16_t)strlen(hs->hdrs[hs->hdr_index]);

      /* How much of this can we send? */
      sendlen = (len < (hdrlen - hs->hdr_pos)) ? len : (hdrlen - hs->hdr_pos);

      /* Send this amount of data or as much as we can given memory
      * constraints. */
      ptr = (const void *)(hs->hdrs[hs->hdr_index] + hs->hdr_pos);
      old_sendlen = sendlen;
      printf("FUCK!!!! %s %d\n\n", ptr, sendlen);
      err = http_write(pcb, ptr, &sendlen, HTTP_IS_HDR_VOLATILE(hs, ptr));
      if ((err == ERR_OK) && (old_sendlen != sendlen)) {
        /* Remember that we added some more data to be transmitted. */
        data_to_send = true;
      } else if (err != ERR_OK) {
         /* special case: http_write does not try to send 1 byte */
        sendlen = 0;
      }

      /* Fix up the header position for the next time round. */
      hs->hdr_pos += sendlen;
      len -= sendlen;

      /* Have we finished sending this string? */
      if(hs->hdr_pos == hdrlen) {
        /* Yes - move on to the next one */
        hs->hdr_index++;
        hs->hdr_pos = 0;
      }
    }

    /* If we get here and there are still header bytes to send, we send
    * the header information we just wrote immediately.  If there are no
    * more headers to send, but we do have file data to send, drop through
    * to try to send some file data too. */
    if((hs->hdr_index < NUM_FILE_HDR_STRINGS) || !hs->file) {
      LWIP_DEBUGF(HTTPD_DEBUG, ("tcp_output\n"));
      return 1;
    }
  }

/* end of sending header*/

  /* Have we run out of file data to send? If so, we need to read the next
   * block from the file. */
  if (hs->left == 0) {
#if LWIP_HTTPD_SSI || LWIP_HTTPD_DYNAMIC_HEADERS
    int count;
#endif /* LWIP_HTTPD_SSI || LWIP_HTTPD_DYNAMIC_HEADERS */

    /* Do we have a valid file handle? */
    if (hs->handle == NULL) {
      /* No - close the connection. */
      printf("[*] http_send_data nothing to send, closed\n");
      http_close_conn(pcb, hs);
      return 0;
    }
    if (webfs_bytes_left(hs->handle) <= 0) {
      /* We reached the end of the file so this request is done.
       * @todo: don't close here for HTTP/1.1? */
      LWIP_DEBUGF(HTTPD_DEBUG, ("End of file.\n"));
      printf("[*] http_send_data EOF\n");
      http_close_conn(pcb, hs);
      return 0;
    }

    /* Do we already have a send buffer allocated? */
    if(hs->buf) {
      /* Yes - get the length of the buffer */
      count = hs->buf_len;
    } else {
      /* We don't have a send buffer so allocate one up to 2mss bytes long. */
      count = 2 * tcp_mss(pcb);
      do {
        hs->buf = (char*)mem_malloc((mem_size_t)count);
        if (hs->buf != NULL) {
          hs->buf_len = count;
          break;
        }
        count = count / 2;
      } while (count > 100);

      /* Did we get a send buffer? If not, return immediately. */
      if (hs->buf == NULL) {
        LWIP_DEBUGF(HTTPD_DEBUG, ("No buff\n"));
        return 0;
      }
    }

    /* Read a block of data from the file. */
    printf("[*] http_send_data trying to read %d bytes\n", count);
    LWIP_DEBUGF(HTTPD_DEBUG, ("Trying to read %d bytes.\n", count));

    count = webfs_read(hs->handle, hs->buf, count);
    if(count < 0) {
      /* We reached the end of the file so this request is done.
       * @todo: don't close here for HTTP/1.1? */
      LWIP_DEBUGF(HTTPD_DEBUG, ("End of file.\n"));
      http_close_conn(pcb, hs);
      return 1;
    }

    /* Set up to send the block of data we just read */
    LWIP_DEBUGF(HTTPD_DEBUG, ("Read %d bytes.\n", count));
    hs->left = count;
    hs->file = hs->buf;
  }

    printf("hs->left: %d\n", hs->left);
    if (tcp_sndbuf(pcb) < hs->left) {
      len = tcp_sndbuf(pcb);
    } else {
      len = (u16_t)hs->left;
      LWIP_ASSERT("hs->left did not fit into u16_t!", (len == hs->left));
    }
    mss = tcp_mss(pcb);
    if(len > (2 * mss)) {
      len = 2 * mss;
    }
    err = http_write(pcb, hs->file, &len, HTTP_IS_DATA_VOLATILE(hs));
    if (err == ERR_OK) {
      data_to_send = true;
      hs->file += len;
      hs->left -= len;
    }

  if((hs->left == 0) && (webfs_bytes_left(hs->handle) <= 0)) {
    /* We reached the end of the file so this request is done.
     * This adds the FIN flag right into the last data segment.
     * @todo: don't close here for HTTP/1.1? */
    LWIP_DEBUGF(HTTPD_DEBUG, ("End of file.\n"));

    http_close_conn(pcb, hs);
    return 0;
  }
  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("send_data end.\n"));
  return data_to_send;
}

#if LWIP_HTTPD_SUPPORT_EXTSTATUS
/** Initialize a http connection with a file to send for an error message
 *
 * @param hs http connection state
 * @param error_nr HTTP error number
 * @return ERR_OK if file was found and hs has been initialized correctly
 *         another err_t otherwise
 */
static err_t ICACHE_FLASH_ATTR
http_find_error_file(struct http_state *hs, u16_t error_nr)
{
  const char *uri1, *uri2, *uri3;
  struct webfs_file *file;

  if (error_nr == 501) {
    uri1 = "/501.html";
    uri2 = "/501.htm";
    uri3 = "/501.shtml";
  } else {
    /* 400 (bad request is the default) */
    uri1 = "/400.html";
    uri2 = "/400.htm";
    uri3 = "/400.shtml";
  }
  file = webfs_open(uri1, hs->req_info);
  if (file == NULL) {
    file = webfs_open(uri2, hs->req_info);
    if (file == NULL) {
      file = webfs_open(uri3, hs->req_info);
      if (file == NULL) {
        LWIP_DEBUGF(HTTPD_DEBUG, ("Error page for error %"U16_F" not found\n",
          error_nr));
        return ERR_ARG;
      }
    }
  }
  return http_init_file(hs, file, 0, NULL);
}
#else /* LWIP_HTTPD_SUPPORT_EXTSTATUS */
#define http_find_error_file(hs, error_nr) ERR_ARG
#endif /* LWIP_HTTPD_SUPPORT_EXTSTATUS */

/**
 * Get the file struct for a 404 error page.
 * Tries some file names and returns NULL if none found.
 *
 * @param uri pointer that receives the actual file name URI
 * @return file struct for the error page or NULL no matching file was found
 */
static struct webfs_file * ICACHE_FLASH_ATTR
http_get_404_file(const char **uri)
{
  printf("[*] http_get_404: uri=%s\n", *uri);
  struct webfs_file *file;

  *uri = "/404.html";
  file = webfs_open(*uri, NULL);
  if(file == NULL) {
    /* 404.html doesn't exist. Try 404.htm instead. */
    *uri = "/404.htm";
    file = webfs_open(*uri, NULL);
    if(file == NULL) {
      /* 404.htm doesn't exist either. Try 404.shtml instead. */
      *uri = "/404.shtml";
      file = webfs_open(*uri, NULL);
      if(file == NULL) {
        /* 404.htm doesn't exist either. Indicate to the caller that it should
         * send back a default 404 page.
         */
        *uri = NULL;
      }
    }
  }

  return file;
}

#if LWIP_HTTPD_SUPPORT_POST
static err_t ICACHE_FLASH_ATTR
http_handle_post_finished(struct http_state *hs)
{
  /* application error or POST finished */
  /* NULL-terminate the buffer */
  //http_post_response_filename[0] = 0;
  httpd_post_finished(hs, http_post_response_filename, LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN);
  printf("[*] httpd_post_finished done, http_post_response_filename=%s\n", http_post_response_filename);
  return http_find_file(hs, hs->req_info.uri, 0);
}

/** Pass received POST body data to the application and correctly handle
 * returning a response document or closing the connection.
 * ATTENTION: The application is responsible for the pbuf now, so don't free it!
 *
 * @param hs http connection state
 * @param p pbuf to pass to the application
 * @return ERR_OK if passed successfully, another err_t if the response file
 *         hasn't been found (after POST finished)
 */
static err_t ICACHE_FLASH_ATTR
http_post_rxpbuf(struct http_state *hs, struct pbuf *p)
{
  printf("[*] http_post_rxpbuf\n");
  err_t err;

  /* adjust remaining Content-Length */
  if (hs->post_content_len_left < p->tot_len) {
    hs->post_content_len_left = 0;
  } else {
    hs->post_content_len_left -= p->tot_len;
  }
  err = httpd_post_receive_data(hs, p);
  if ((err != ERR_OK) || (hs->post_content_len_left == 0)) {
#if LWIP_HTTPD_SUPPORT_POST && LWIP_HTTPD_POST_MANUAL_WND
    if (hs->unrecved_bytes != 0) {
       return ERR_OK;
    }
#endif /* LWIP_HTTPD_SUPPORT_POST && LWIP_HTTPD_POST_MANUAL_WND */
    /* application error or POST finished */
    return http_handle_post_finished(hs);
  }

  return ERR_OK;
}

/** Handle a post request. Called from http_parse_request when method 'POST'
 * is found.
 *
 * @param pcb The tcp_pcb which received this packet.
 * @param p The input pbuf (containing the POST header and body).
 * @param hs The http connection state.
 * @param data HTTP request (header and part of body) from input pbuf(s).
 * @param data_len Size of 'data'.
 * @param uri The HTTP URI parsed from input pbuf(s).
 * @param uri_end Pointer to the end of 'uri' (here, the rest of the HTTP
 *                header starts).
 * @return ERR_OK: POST correctly parsed and accepted by the application.
 *         ERR_INPROGRESS: POST not completely parsed (no error yet)
 *         another err_t: Error parsing POST or denied by the application
 */
static err_t ICACHE_FLASH_ATTR
http_post_request(struct tcp_pcb *pcb, struct pbuf **inp, struct http_state *hs,
                  char *data, u16_t data_len, char *uri, char *uri_end)
{
  printf("[*] HTTP POST REQUEST uri=%s\n", uri);
  err_t err;
  /* search for end-of-header (first double-CRLF) */
  char* crlfcrlf = strnstr(uri_end + 1, CRLF CRLF, data_len - (uri_end + 1 - data));

#if LWIP_HTTPD_POST_MANUAL_WND
  hs->pcb = pcb;
#else /* LWIP_HTTPD_POST_MANUAL_WND */
  LWIP_UNUSED_ARG(pcb); /* only used for LWIP_HTTPD_POST_MANUAL_WND */
#endif /*  LWIP_HTTPD_POST_MANUAL_WND */

  if (crlfcrlf != NULL) {
    /* search for "Content-Length: " */
#define HTTP_HDR_CONTENT_LEN                "Content-Length: "
#define HTTP_HDR_CONTENT_LEN_LEN            16
#define HTTP_HDR_CONTENT_LEN_DIGIT_MAX_LEN  10
    char *scontent_len = strnstr(uri_end + 1, HTTP_HDR_CONTENT_LEN, crlfcrlf - (uri_end + 1));
    if (scontent_len != NULL) {
      char *scontent_len_end = strnstr(scontent_len + HTTP_HDR_CONTENT_LEN_LEN, CRLF, HTTP_HDR_CONTENT_LEN_DIGIT_MAX_LEN);
      if (scontent_len_end != NULL) {
        int content_len;
        char *conten_len_num = scontent_len + HTTP_HDR_CONTENT_LEN_LEN;
        *scontent_len_end = 0;
        content_len = atoi(conten_len_num);
        if (content_len > 0) {
          /* adjust length of HTTP header passed to application */
          const char *hdr_start_after_uri = uri_end + 1;
          u16_t hdr_len = LWIP_MIN(data_len, crlfcrlf + 4 - data);
          u16_t hdr_data_len = LWIP_MIN(data_len, crlfcrlf + 4 - hdr_start_after_uri);
          u8_t post_auto_wnd = 1;
          http_post_response_filename[0] = 0;
          hs->req_info.uri = uri;
          err = httpd_post_begin(hs, uri, hdr_start_after_uri, hdr_data_len, content_len,
            http_post_response_filename, LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN, &post_auto_wnd);
          if (err == ERR_OK) {
            /* try to pass in data of the first pbuf(s) */
            struct pbuf *q = *inp;
            u16_t start_offset = hdr_len;
#if LWIP_HTTPD_POST_MANUAL_WND
            hs->no_auto_wnd = !post_auto_wnd;
#endif /* LWIP_HTTPD_POST_MANUAL_WND */
            /* set the Content-Length to be received for this POST */
            hs->post_content_len_left = (u32_t)content_len;

            /* get to the pbuf where the body starts */
            while((q != NULL) && (q->len <= start_offset)) {
              struct pbuf *head = q;
              start_offset -= q->len;
              q = q->next;
              /* free the head pbuf */
              head->next = NULL;
              pbuf_free(head);
            }
            *inp = NULL;
            if (q != NULL) {
              /* hide the remaining HTTP header */
              pbuf_header(q, -(s16_t)start_offset);
#if LWIP_HTTPD_POST_MANUAL_WND
              if (!post_auto_wnd) {
                /* already tcp_recved() this data... */
                hs->unrecved_bytes = q->tot_len;
              }
#endif /* LWIP_HTTPD_POST_MANUAL_WND */
              return http_post_rxpbuf(hs, q);
            } else {
              return ERR_OK;
            }
          } else {
            /* return file passed from application */
            return http_find_file(hs, http_post_response_filename, 0);
          }
        } else {
          LWIP_DEBUGF(HTTPD_DEBUG, ("POST received invalid Content-Length: %s\n",
            conten_len_num));
          return ERR_ARG;
        }
      }
    }
  }
  /* if we come here, the POST is incomplete */
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
  return ERR_INPROGRESS;
#else /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
  return ERR_ARG;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
}
#if LWIP_HTTPD_SUPPORT_POST
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
  printf("[*] http_post_begin: uri=%s\n", uri);
struct http_state *hs = (struct http_state *)connection;

 if(!uri || (uri[0] == '\0')) {
    return ERR_ARG;
 }
  return ERR_OK;
}

#define LWIP_HTTPD_POST_MAX_PAYLOAD_LEN     512
static char http_post_payload[LWIP_HTTPD_POST_MAX_PAYLOAD_LEN];
static u16_t http_post_payload_len = 0;

err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    printf("[*] httpd_post_receive_data invoked \n");
    struct http_state *hs = (struct http_state *)connection;
    struct pbuf *q = p;
    int count;
    u32_t http_post_payload_full_flag = 0;
    while(q != NULL)  // 缓存接收的数据至http_post_payload
    {
      if(http_post_payload_len + q->len <= LWIP_HTTPD_POST_MAX_PAYLOAD_LEN) {
          MEMCPY(http_post_payload+http_post_payload_len, q->payload, q->len);
          http_post_payload_len += q->len;
      }
      else {  // 缓存溢出 置溢出标志位
        http_post_payload_full_flag = 1;
        break;
      }
      q = q->next;
    }

    pbuf_free(p); // 释放pbuf

    if(http_post_payload_full_flag) // 缓存溢出 则丢弃数据
    {
        http_post_payload_full_flag = 0;
        http_post_payload_len = 0;
    }else if(hs->post_content_len_left == 0) {  // POST数据已经接收完毕 则处理
        printf("[*] payload=%s \n", http_post_payload);
        hs->req_info.post_data = http_post_payload;
        //count = extract_uri_parameters(hs, http_post_payload);  // 解析
        printf("[*] POST PARAMETERS: %s, %s, payload=%s", hs->params, hs->param_vals, http_post_payload);
        http_post_payload_len = 0;
    }
    return ERR_OK;
}

 
void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    printf("[*] httpd_post_finished invoked, uri=%s", response_uri);
    struct http_state *hs = (struct http_state *)connection;
    printf("[*] httpd_post_finished, uri=%s", hs->req_info.uri);
}

/* LWIP_HTTPD_SUPPORT_POST END */
#endif 



#if LWIP_HTTPD_POST_MANUAL_WND
/** A POST implementation can call this function to update the TCP window.
 * This can be used to throttle data reception (e.g. when received data is
 * programmed to flash and data is received faster than programmed).
 *
 * @param connection A connection handle passed to httpd_post_begin for which
 *        httpd_post_finished has *NOT* been called yet!
 * @param recved_len Length of data received (for window update)
 */
void ICACHE_FLASH_ATTR
httpd_post_data_recved(void *connection, u16_t recved_len)
{
  printf("[*] httpd_post_data_recved invoked\n");
  struct http_state *hs = (struct http_state*)connection;
  if (hs != NULL) {
    if (hs->no_auto_wnd) {
      u16_t len = recved_len;
      if (hs->unrecved_bytes >= recved_len) {
        hs->unrecved_bytes -= recved_len;
      } else {
        LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_LEVEL_WARNING, ("httpd_post_data_recved: recved_len too big\n"));
        len = (u16_t)hs->unrecved_bytes;
        hs->unrecved_bytes = 0;
      }
      if (hs->pcb != NULL) {
        if (len != 0) {
          tcp_recved(hs->pcb, len);
        }
        if ((hs->post_content_len_left == 0) && (hs->unrecved_bytes == 0)) {
          /* finished handling POST */
          http_handle_post_finished(hs);
          http_send_data(hs->pcb, hs);
        }
      }
    }
  }
}
#endif /* LWIP_HTTPD_POST_MANUAL_WND */

#endif /* LWIP_HTTPD_SUPPORT_POST */

/**
 * When data has been received in the correct state, try to parse it
 * as a HTTP request.
 *
 * @param p the received pbuf
 * @param hs the connection state
 * @param pcb the tcp_pcb which received this packet
 * @return ERR_OK if request was OK and hs has been initialized correctly
 *         ERR_INPROGRESS if request was OK so far but not fully received
 *         another err_t otherwise
 */
static err_t ICACHE_FLASH_ATTR
http_parse_request(struct pbuf **inp, struct http_state *hs, struct tcp_pcb *pcb)
{
  printf("[*] http_parse_request invoked\n");
  char *data;
  char *crlf;
  u16_t data_len;
  struct pbuf *p = *inp;
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
  u16_t clen;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
#if LWIP_HTTPD_SUPPORT_POST
  err_t err;
#endif /* LWIP_HTTPD_SUPPORT_POST */

  LWIP_UNUSED_ARG(pcb); /* only used for post */
  LWIP_ASSERT("p != NULL", p != NULL);
  LWIP_ASSERT("hs != NULL", hs != NULL);
  /* first we set hs->if_post = 0 */
  hs->req_info.is_post = 0;
  if ((hs->handle != NULL) || (hs->file != NULL)) {
    LWIP_DEBUGF(HTTPD_DEBUG, ("Received data while sending a file\n"));
    /* already sending a file */
    /* @todo: abort? */
    return ERR_USE;
  }

#if LWIP_HTTPD_SUPPORT_REQUESTLIST

  LWIP_DEBUGF(HTTPD_DEBUG, ("Received %"U16_F" bytes\n", p->tot_len));

  /* first check allowed characters in this pbuf? */

  /* enqueue the pbuf */
  if (hs->req == NULL) {
    LWIP_DEBUGF(HTTPD_DEBUG, ("First pbuf\n"));
    hs->req = p;
  } else {
    LWIP_DEBUGF(HTTPD_DEBUG, ("pbuf enqueued\n"));
    pbuf_cat(hs->req, p);
  }

  if (hs->req->next != NULL) {
    data_len = LWIP_MIN(hs->req->tot_len, LWIP_HTTPD_MAX_REQ_LENGTH);
    pbuf_copy_partial(hs->req, httpd_req_buf, data_len, 0);
    data = httpd_req_buf;
  } else
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
  {
    data = (char *)p->payload;
    data_len = p->len;
    if (p->len != p->tot_len) {
      LWIP_DEBUGF(HTTPD_DEBUG, ("Warning: incomplete header due to chained pbufs\n"));
    }
  }

  /* received enough data for minimal request? */
  if (data_len >= MIN_REQ_LEN) {
    /* wait for CRLF before parsing anything */
    crlf = strnstr(data, CRLF, data_len);
    if (crlf != NULL) {
#if LWIP_HTTPD_SUPPORT_POST
      int is_post = 0;
#endif /* LWIP_HTTPD_SUPPORT_POST */
      int is_09 = 0;
      char *sp1, *sp2;
      u16_t left_len, uri_len;
      LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("CRLF received, parsing request\n"));
      /* parse method */
      if (!strncmp(data, "GET ", 4)) {
        sp1 = data + 3;
        /* received GET request */
        LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Received GET request\"\n"));
#if LWIP_HTTPD_SUPPORT_POST
      } else if (!strncmp(data, "POST ", 5)) {
        /* store request type */
        is_post = 1;
        sp1 = data + 4;
        /* received GET request */
        LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Received POST request\n"));
#endif /* LWIP_HTTPD_SUPPORT_POST */
      } else {
        /* null-terminate the METHOD (pbuf is freed anyway wen returning) */
        data[4] = 0;
        /* unsupported method! */
        LWIP_DEBUGF(HTTPD_DEBUG, ("Unsupported request method (not implemented): \"%s\"\n",
          data));
        return http_find_error_file(hs, 501);
      }
      /* if we come here, method is OK, parse URI */
      left_len = data_len - ((sp1 +1) - data);
      sp2 = strnstr(sp1 + 1, " ", left_len);
#if LWIP_HTTPD_SUPPORT_V09
      if (sp2 == NULL) {
        /* HTTP 0.9: respond with correct protocol version */
        sp2 = strnstr(sp1 + 1, CRLF, left_len);
        is_09 = 1;
#if LWIP_HTTPD_SUPPORT_POST
        if (is_post) {
          /* HTTP/0.9 does not support POST */
          goto badrequest;
        }
#endif /* LWIP_HTTPD_SUPPORT_POST */
      }
#endif /* LWIP_HTTPD_SUPPORT_V09 */
      uri_len = sp2 - (sp1 + 1);
      if ((sp2 != 0) && (sp2 > sp1)) {
        char *uri = sp1 + 1;
        /* null-terminate the METHOD (pbuf is freed anyway wen returning) */
        *sp1 = 0;
        uri[uri_len] = 0;
        LWIP_DEBUGF(HTTPD_DEBUG, ("Received \"%s\" request for URI: \"%s\"\n",
                    data, uri));
#if LWIP_HTTPD_SUPPORT_POST
        if (is_post) {
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
          struct pbuf **q = &hs->req;
#else /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
          struct pbuf **q = inp;
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
          hs->req_info.is_post = 1;
          err = http_post_request(pcb, q, hs, data, data_len, uri, sp2);
          if (err != ERR_OK) {
            /* restore header for next try */
            *sp1 = ' ';
            *sp2 = ' ';
            uri[uri_len] = ' ';
          }
          if (err == ERR_ARG) {
            goto badrequest;
          }
          return err;
        } else
#endif /* LWIP_HTTPD_SUPPORT_POST */
        {
          return http_find_file(hs, uri, is_09);
        }
      } else {
        LWIP_DEBUGF(HTTPD_DEBUG, ("invalid URI\n"));
      }
    }
  }

#if LWIP_HTTPD_SUPPORT_REQUESTLIST
  clen = pbuf_clen(hs->req);
  if ((hs->req->tot_len <= LWIP_HTTPD_REQ_BUFSIZE) &&
    (clen <= LWIP_HTTPD_REQ_QUEUELEN)) {
    /* request not fully received (too short or CRLF is missing) */
    return ERR_INPROGRESS;
  } else
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
  {
#if LWIP_HTTPD_SUPPORT_POST
badrequest:
#endif /* LWIP_HTTPD_SUPPORT_POST */
    LWIP_DEBUGF(HTTPD_DEBUG, ("bad request\n"));
    /* could not parse request */
    return http_find_error_file(hs, 400);
  }
}

/** Try to find the file specified by uri and, if found, initialize hs
 * accordingly.
 *
 * @param hs the connection state
 * @param uri the HTTP header URI
 * @param is_09 1 if the request is HTTP/0.9 (no HTTP headers in response)
 * @return ERR_OK if file was found and hs has been initialized correctly
 *         another err_t otherwise
 */
static err_t ICACHE_FLASH_ATTR
http_find_file(struct http_state *hs, const char *uri, int is_09)
{
  size_t loop;
  struct webfs_file *file = NULL;
  char *params;

  hs->req_info.params = NULL;
  params = (char *)strchr(uri, '?');
  if (params != NULL) {
    /* URI contains parameters. NULL-terminate the base URI */
    *params = '\0';
    params++;
    hs->req_info.params = params;
  }


  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("Opening %s\n", uri));
  printf("[*] http_find_file: file open %s\n", uri);
  /* we pass http_state into webfs_open */
  file = webfs_open(uri, (void *)&hs->req_info);
  if (file == NULL) {
    printf("[*] http_find_file: %s 404\n", uri);
    file = http_get_404_file(&uri);
  }
  printf("[*] http_find_file: file open %s done\n", uri);

  return http_init_file(hs, file, is_09, uri);
}

/** Initialize a http connection with a file to send (if found).
 * Called by http_find_file and http_find_error_file.
 *
 * @param hs http connection state
 * @param file file structure to send (or NULL if not found)
 * @param is_09 1 if the request is HTTP/0.9 (no HTTP headers in response)
 * @param uri the HTTP header URI
 * @return ERR_OK if file was found and hs has been initialized correctly
 *         another err_t otherwise
 */
static err_t ICACHE_FLASH_ATTR
http_init_file(struct http_state *hs, struct webfs_file *file, int is_09, const char *uri)
{
  printf("[*] http_init_file invoked\n");
  printf("[*] http_init_file file content: %s", file->data);
  if (file != NULL) {
    /* file opened, initialise struct http_state */
    hs->handle = file;
    hs->file = (char*)file->data;
    LWIP_ASSERT("File length must be positive!", (file->len >= 0));
    hs->left = file->len;
    hs->retries = 0;
#if LWIP_HTTPD_TIMING
    hs->time_started = sys_now();
#endif /* LWIP_HTTPD_TIMING */
#if !LWIP_HTTPD_DYNAMIC_HEADERS
    LWIP_ASSERT("HTTP headers not included in file system", hs->handle->http_header_included);
#endif /* !LWIP_HTTPD_DYNAMIC_HEADERS */
#if LWIP_HTTPD_SUPPORT_V09
    if (hs->handle->http_header_included && is_09) {
      /* HTTP/0.9 responses are sent without HTTP header,
         search for the end of the header. */
      char *file_start = strnstr(hs->file, CRLF CRLF, hs->left);
      if (file_start != NULL) {
        size_t diff = file_start + 4 - hs->file;
        hs->file += diff;
        hs->left -= (u32_t)diff;
      }
    }
#endif /* LWIP_HTTPD_SUPPORT_V09*/
  } else {
    hs->handle = NULL;
    hs->file = NULL;
    hs->left = 0;
    hs->retries = 0;
  }
#if LWIP_HTTPD_DYNAMIC_HEADERS
    /* Determine the HTTP headers to send based on the file extension of
   * the requested URI. */
  if ((hs->handle == NULL) || !hs->handle->http_header_included) {
    printf("[*] HTTP GET HEADER INVOKED");
    get_http_headers(hs, (char*)uri);
  }
#else /* LWIP_HTTPD_DYNAMIC_HEADERS */
  LWIP_UNUSED_ARG(uri);
#endif /* LWIP_HTTPD_DYNAMIC_HEADERS */
  return ERR_OK;
}

/**
 * The pcb had an error and is already deallocated.
 * The argument might still be valid (if != NULL).
 */
static void ICACHE_FLASH_ATTR
http_err(void *arg, err_t err)
{
  printf("[*] http_err invoked\n");
  struct http_state *hs = (struct http_state *)arg;
  LWIP_UNUSED_ARG(err);

  LWIP_DEBUGF(HTTPD_DEBUG, ("http_err: %s", lwip_strerr(err)));

  if (hs != NULL) {
    http_state_free(hs);
  }
}

/**
 * Data has been sent and acknowledged by the remote host.
 * This means that more data can be sent.
 */
static err_t ICACHE_FLASH_ATTR
http_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
  printf("[*] http_sent invoked\n");
  struct http_state *hs = (struct http_state *)arg;

  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("http_sent %p\n", (void*)pcb));

  LWIP_UNUSED_ARG(len);

  if (hs == NULL) {
    return ERR_OK;
  }

  hs->retries = 0;

  http_send_data(pcb, hs);

  return ERR_OK;
}

/**
 * The poll function is called every 2nd second.
 * If there has been no data sent (which resets the retries) in 8 seconds, close.
 * If the last portion of a file has not been sent in 2 seconds, close.
 *
 * This could be increased, but we don't want to waste resources for bad connections.
 */
static err_t ICACHE_FLASH_ATTR
http_poll(void *arg, struct tcp_pcb *pcb)
{
  printf("[*] http_poll invoked\n");
  struct http_state *hs = (struct http_state *)arg;
  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("http_poll: pcb=%p hs=%p pcb_state=%s\n",
    (void*)pcb, (void*)hs, tcp_debug_state_str(pcb->state)));

  if (hs == NULL) {
    err_t closed;
    /* arg is null, close. */
    LWIP_DEBUGF(HTTPD_DEBUG, ("http_poll: arg is NULL, close\n"));
    closed = http_close_conn(pcb, hs);
    LWIP_UNUSED_ARG(closed);
#if LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR
    if (closed == ERR_MEM) {
       tcp_abort(pcb);
       return ERR_ABRT;
    }
#endif /* LWIP_HTTPD_ABORT_ON_CLOSE_MEM_ERROR */
    return ERR_OK;
  } else {
    hs->retries++;
    if (hs->retries == HTTPD_MAX_RETRIES) {
      LWIP_DEBUGF(HTTPD_DEBUG, ("http_poll: too many retries, close\n"));
      http_close_conn(pcb, hs);
      return ERR_OK;
    }

    /* If this connection has a file open, try to send some more data. If
     * it has not yet received a GET request, don't do this since it will
     * cause the connection to close immediately. */
    if(hs && (hs->handle)) {
      LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("http_poll: try to send more data\n"));
      if(http_send_data(pcb, hs)) {
        /* If we wrote anything to be sent, go ahead and send it now. */
        LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("tcp_output\n"));
        tcp_output(pcb);
      }
    }
  }

  return ERR_OK;
}

/**
 * Data has been received on this pcb.
 * For HTTP 1.0, this should normally only happen once (if the request fits in one packet).
 */
static err_t ICACHE_FLASH_ATTR
http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  err_t parsed = ERR_ABRT;
  struct http_state *hs = (struct http_state *)arg;
  LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("http_recv: pcb=%p pbuf=%p err=%s\n", (void*)pcb,
    (void*)p, lwip_strerr(err)));

  if ((err != ERR_OK) || (p == NULL) || (hs == NULL)) {
    /* error or closed by other side? */
    if (p != NULL) {
      /* Inform TCP that we have taken the data. */
      tcp_recved(pcb, p->tot_len);
      pbuf_free(p);
    }
    if (hs == NULL) {
      /* this should not happen, only to be robust */
      LWIP_DEBUGF(HTTPD_DEBUG, ("Error, http_recv: hs is NULL, close\n"));
    }
    http_close_conn(pcb, hs);
    return ERR_OK;
  }

#if LWIP_HTTPD_SUPPORT_POST && LWIP_HTTPD_POST_MANUAL_WND
  if (hs->no_auto_wnd) {
     hs->unrecved_bytes += p->tot_len;
  } else
#endif /* LWIP_HTTPD_SUPPORT_POST && LWIP_HTTPD_POST_MANUAL_WND */
  {
    /* Inform TCP that we have taken the data. */
    tcp_recved(pcb, p->tot_len);
  }

  if (hs->post_content_len_left > 0) {
    /* reset idle counter when POST data is received */
    hs->retries = 0;
    /* this is data for a POST, pass the complete pbuf to the application */
    http_post_rxpbuf(hs, p);
    /* pbuf is passed to the application, don't free it! */
    if (hs->post_content_len_left == 0) {
      /* all data received, send response or close connection */
      http_send_data(pcb, hs);
    }
    return ERR_OK;
  } else
  {
    if (hs->handle == NULL) {
      parsed = http_parse_request(&p, hs, pcb);
      LWIP_ASSERT("http_parse_request: unexpected return value", parsed == ERR_OK
        || parsed == ERR_INPROGRESS ||parsed == ERR_ARG || parsed == ERR_USE);
    } else {
      LWIP_DEBUGF(HTTPD_DEBUG, ("http_recv: already sending data\n"));
    }
#if LWIP_HTTPD_SUPPORT_REQUESTLIST
    if (parsed != ERR_INPROGRESS) {
      /* request fully parsed or error */
      if (hs->req != NULL) {
        pbuf_free(hs->req);
        hs->req = NULL;
      }
    }
#else /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
    if (p != NULL) {
      /* pbuf not passed to application, free it now */
      pbuf_free(p);
    }
#endif /* LWIP_HTTPD_SUPPORT_REQUESTLIST */
    if (parsed == ERR_OK) {
      if (hs->post_content_len_left == 0)
      {
        LWIP_DEBUGF(HTTPD_DEBUG | LWIP_DBG_TRACE, ("http_recv: data %p len %"S32_F"\n", hs->file, hs->left));
        printf("[*] http_recv invoked\n");
        http_send_data(pcb, hs);
      }
    } else if (parsed == ERR_ARG) {
      /* @todo: close on ERR_USE? */
      http_close_conn(pcb, hs);
    }
  }
  return ERR_OK;
}

/**
 * A new incoming connection has been accepted.
 */
static err_t ICACHE_FLASH_ATTR
http_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
  struct http_state *hs;
  struct tcp_pcb_listen *lpcb = (struct tcp_pcb_listen*)arg;
  LWIP_UNUSED_ARG(err);
  LWIP_DEBUGF(HTTPD_DEBUG, ("http_accept %p / %p\n", (void*)pcb, arg));

  /* Decrease the listen backlog counter */
  tcp_accepted(lpcb);
  /* Set priority */
  tcp_setprio(pcb, HTTPD_TCP_PRIO);

  /* Allocate memory for the structure that holds the state of the
     connection - initialized by that function. */
  hs = http_state_alloc();
  if (hs == NULL) {
    LWIP_DEBUGF(HTTPD_DEBUG, ("http_accept: Out of memory, RST\n"));
    return ERR_MEM;
  }

  /* Tell TCP that this is the structure we wish to be passed for our
     callbacks. */
  tcp_arg(pcb, hs);

  /* Set up the various callback functions */
  tcp_recv(pcb, http_recv);
  tcp_err(pcb, http_err);
  tcp_poll(pcb, http_poll, HTTPD_POLL_INTERVAL);
  tcp_sent(pcb, http_sent);

  return ERR_OK;
}

/**
 * Initialize the httpd with the specified local address.
 */
static void ICACHE_FLASH_ATTR
httpd_init_addr(ip_addr_t *local_addr)
{
  struct tcp_pcb *pcb;
  err_t err;

  pcb = tcp_new();
  LWIP_ASSERT("httpd_init: tcp_new failed", pcb != NULL);
  tcp_setprio(pcb, HTTPD_TCP_PRIO);
  /* set SOF_REUSEADDR here to explicitly bind httpd to multiple interfaces */
  err = tcp_bind(pcb, local_addr, HTTPD_SERVER_PORT);
  LWIP_ASSERT("httpd_init: tcp_bind failed", err == ERR_OK);
  pcb = tcp_listen(pcb);
  LWIP_ASSERT("httpd_init: tcp_listen failed", pcb != NULL);
  /* initialize callback arg and accept callback */
  tcp_arg(pcb, pcb);
  tcp_accept(pcb, http_accept);
}

/**
 * Initialize the httpd: set up a listening PCB and bind it to the defined port
 */
void ICACHE_FLASH_ATTR
httpd_init(const u8_t * romfs)
{
#if HTTPD_USE_MEM_POOL
  LWIP_ASSERT("memp_sizes[MEMP_HTTPD_STATE] >= sizeof(http_state)",
     memp_sizes[MEMP_HTTPD_STATE] >= sizeof(http_state));
#endif
  LWIP_DEBUGF(HTTPD_DEBUG, ("httpd_init\n"));

  httpd_init_addr(IP_ADDR_ANY);
  printf("[*] init webfs\n");
  webfs_init(romfs);
}


#endif /* LWIP_TCP */
