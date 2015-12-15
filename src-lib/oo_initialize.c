/*
  oo_initialize - init code for OSDP

  (C)Copyright 2014-2015 Smithee,Spelvin,Agnew & Plinge, Inc.

  Support provided by the Security Industry Association
  http://www.securityindustry.org

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at
 
    http://www.apache.org/licenses/LICENSE-2.0
 
  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>


#include <gnutls/gnutls.h>


#include <osdp-tls.h>
#include <open-osdp.h>
#include <osdp_conformance.h>


extern OSDP_INTEROP_ASSESSMENT
  osdp_conformance;
extern OSDP_PARAMETERS
  p_card;


int
  init_serial
    (OSDP_CONTEXT
      *context,
    char
      *device)

{ /* init_serial */

  char
    command [1024];
  int
    status;
  int
    status_io;

  if (m_verbosity > 3)
    printf ("init_serial: command %s\n",
      context->init_command);
  if (strlen (context->init_command) > 0)
  {
    fprintf (context->log, "Using init command \"%s\" for device %s\n",
      context->init_command, device);
    sprintf (command, context->init_command, device);
    system (command);
  };
  fprintf (stderr, "Opening %s\n", device);
  context->fd = open (device, O_RDWR | O_NONBLOCK);
  if (context->fd EQUALS -1)
  {
    fprintf (stderr, "errno at device %s open %d\n", device, errno);
    status= ST_SERIAL_OPEN_ERR;
  }
  else
    status = ST_OK;
  if (status EQUALS ST_OK)
  {
    cfmakeraw (&(context->tio));
    status_io = tcsetattr (context->fd, TCSANOW, &(context->tio));
    if (status_io != 0)
      status = ST_SERIAL_SET_ERR;
  };

  return (status);

} /* init_serial */


int
  initialize_osdp
    (OSDP_CONTEXT
      *context)

{ /* initialize */

  extern unsigned char
    *creds_buffer_a;
  extern int
    creds_buffer_a_lth;
  extern int
    creds_buffer_a_next;
  extern int
    creds_buffer_a_remaining;
  int
    creds_f;
  char
    logmsg [1024];
  extern char
    *multipart_message_buffer_1;
  char
    optstring [1024];
  extern OSDP_BUFFER
    *osdp_buf;
  extern time_t
    previous_time;
  int
    status;
  int
    status_io;


  status = ST_OK;
  memset (context, 0, sizeof (*context));
  memset (&osdp_conformance, 0, sizeof (osdp_conformance));
  context->mmsgbuf = multipart_message_buffer_1;
  memset (&p_card, 0, sizeof (p_card));

  m_verbosity = 3;
  m_version_minor = OSDP_VERSION_MINOR;
  m_build = OSDP_VERSION_BUILD;
  context->model = 2;
  context->version = 1;
  context->fw_version [0] = OSDP_VERSION_MAJOR;
  context->fw_version [1] = m_version_minor;
  context->fw_version [2] = m_build;
  context->vendor_code [0] = 0x08;
  context->vendor_code [1] = 0x00;
  context->vendor_code [2] = 0x1b;
  m_idle_timeout = 30;
  m_check = OSDP_CRC;
  m_dump = 1;
  strcpy (p_card.filename, "/dev/ttyUSB0");
  context->next_sequence = 0;

  previous_time = 0;

  context->log = fopen ("open-osdp.log", "w");
  if (context->log EQUALS NULL)
    status = ST_LOG_OPEN_ERR;

//was ok here
  if (status EQUALS ST_OK)
  {
    /*
      try to get configuration from configuration file open_osdp.cfg
    */
    status = read_config (context);
m_idle_timeout = 30;
    if (m_verbosity > 3)
      fprintf (stderr, "read_config returned %d\n", status);
    status = ST_OK; // doesn't matter if config reading failed
  };

  if (status EQUALS ST_OK)
  {
    strcpy (optstring, "");
  }

  if (status EQUALS ST_OK)
  {
    sprintf (logmsg, "Open-OSDP - OSDP Tester Version %d.%d Build %d",
      context->fw_version [0], m_version_minor, m_build);
    fprintf (context->log, "%s\n", logmsg);
    printf ("%s\n", logmsg);
  };
  if (status EQUALS ST_OK)
  {
    time_t tmp_time;

    struct timespec
      current_time_fine;


    clock_gettime (CLOCK_REALTIME, &current_time_fine);
    tmp_time = time (NULL);
    sprintf (logmsg, "%08ld.%08ld %s(%ld)",
      (unsigned long int)current_time_fine.tv_sec, current_time_fine.tv_nsec,
      asctime (localtime (&tmp_time)), tmp_time);
    fprintf (context->log,
      "%s (Rcvd Frame %6d)", logmsg, context->packets_received);
  };

  fprintf (context->log, "Verbosity set to %d.\n",
    m_verbosity);
  if (context->special_1 != 0)
    fprintf (context->log, "SPECIAL Processing (%d) Enabled\n",
      context->special_1);
  {
    int idx;
    char logmsg [1024];
    char tlogmsg [1024];

    sprintf (logmsg, "Parameters:");
    fprintf (stderr, "%s\n", logmsg);
    fprintf (context->log, "%s\n", logmsg);
    sprintf (logmsg, "  Filename: %s", p_card.filename);
    fprintf (stderr, "%s\n", logmsg);
    fprintf (context->log, "%s\n", logmsg);
    sprintf (logmsg, "  Addr: %02x (%d.)", p_card.addr, p_card.addr);
    fprintf (stderr, "%s\n", logmsg);
    fprintf (context->log, "%s\n", logmsg);
    sprintf (logmsg, "  Bits: %d", p_card.bits);
    fprintf (stderr, "%s\n", logmsg);
    fprintf (context->log, "%s\n", logmsg);
    sprintf (logmsg, "  Value (%d. bytes): ", p_card.value_len);
    fprintf (stderr, "%s", logmsg);
    fprintf (context->log, "%s", logmsg);
    logmsg [0] = 0;
    for (idx=0; idx<p_card.value_len; idx++)
    {
      sprintf (tlogmsg, "%02x", p_card.value [idx]);
      strcat (logmsg, tlogmsg);
    };
    fprintf (stderr, "%s\n", logmsg);
    fprintf (context->log, "%s\n", logmsg);
  };

  if (status EQUALS ST_OK)
  {
    char
      creds_filename_a [1024];

    creds_buffer_a_next = 0;
    creds_buffer_a_remaining = 0;
    strcpy (creds_filename_a, "open-osdp-creds-a.dat");
    fprintf (context->log, "Credentials File A: %s\n", creds_filename_a);

    // initialize credentials buffer(s)

    creds_f = open (creds_filename_a, O_RDONLY);
    if (creds_f != -1)
    {
      status_io = read (creds_f, creds_buffer_a, sizeof (creds_buffer_a));
      if (status_io < 10000)
      {
        creds_buffer_a_lth = status_io;
        fprintf (context->log, "%d. bytes read from credentials file A\n", creds_buffer_a_lth);
        status = ST_OK;
      }
      else
        status = ST_ERR_INIT_CREDS;
    }
    else
    {
      // ignore error
      creds_buffer_a_lth = 0;
      status = ST_OK;
    };
  };
  
  memset (&osdp_buf, 0, sizeof (osdp_buf));
  context->current_menu = OSDP_MENU_TOP;
  return (status);

} /* initialize_osdp */
