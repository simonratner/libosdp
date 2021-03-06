/*
  oosdp-util - open osdp utility routines

  (C)2014-2017 Smithee Spelvin Agnew & Plinge, Inc.

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
#include <memory.h>
#include <unistd.h>
#include <time.h>


#include <gnutls/gnutls.h>


#include <osdp-tls.h>
#include <open-osdp.h>
#include <osdp_conformance.h>


extern OSDP_CONTEXT
  context;
extern OSDP_INTEROP_ASSESSMENT
  osdp_conformance;
extern OSDP_PARAMETERS
  p_card;
time_t
  previous_time;
char
  tlogmsg [1024];
char
  tlogmsg2 [1024];


int
  osdp_build_message
    (unsigned char
        *buf,
    int
      *updated_length,
    unsigned char
      command,
    int
      dest_addr,
    int
      sequence,
    int
      data_length,
    unsigned char
      *data,
    int
      secure)

{ /* osdp_build_mesage */

  int
    check_size;
  unsigned char
    *cmd_ptr;
  int
    new_length;
  unsigned char
    *next_data;
  OSDP_HDR
    *p;
  int
    status;
  int
    whole_msg_lth;


  status = ST_OK;
  if (m_check EQUALS OSDP_CHECKSUM)
    check_size = 1;
  else
    check_size = 2;
  new_length = *updated_length;

  p = (OSDP_HDR *)buf;
  p->som = C_SOM;
  new_length ++;

  // addr
  p->addr = dest_addr;
  // if we're the PD set the high order bit
  if (context.role EQUALS OSDP_ROLE_PD)
    p->addr = p->addr | 0x80;

  new_length ++;

  // length

  /*
    length goes in before CRC calc.
    length is 5 (fields to CTRL) + [if no sec] 1 for CMND + data
  */
  whole_msg_lth = 5;
  if (secure != 0)
status = -2;
  else
    whole_msg_lth = whole_msg_lth + 1; //CMND
  whole_msg_lth = whole_msg_lth + data_length;
  whole_msg_lth = whole_msg_lth + check_size; // including CRC

  p->len_lsb = 0x00ff & whole_msg_lth;
  new_length ++;
  p->len_msb = (0xff00 & whole_msg_lth) >> 8;
  new_length ++;

  // control
  p->ctrl = 0;
  p->ctrl = p->ctrl | (0x3 & sequence);

  // set CRC depending on current value of global parameter
  if (m_check EQUALS OSDP_CRC)
    p->ctrl = p->ctrl | 0x04;

  new_length ++;

  // secure is bit 3 (mask 0x08)
  if (secure)
  {
    p->ctrl = p->ctrl | 0x08;
    cmd_ptr = buf + 8; // STUB pretend sec blk is 3 bytes len len 1 payload
  }
  else
  {
    cmd_ptr = buf + 5; // skip security stuff
  };
// hard-coded off for now
if (secure != 0)
  status = -1;
  
  *cmd_ptr = command;
  new_length++;
  next_data = 1+cmd_ptr;

  if (data_length > 0)
  {
    int i;
    unsigned char *sptr;
    sptr = cmd_ptr + 1;
    if (context.verbosity > 9)
      fprintf (stderr, "orig next_data %lx\n", (unsigned long)next_data);
    for (i=0; i<data_length; i++)
    {
      *(sptr+i) = *(i+data);
      new_length ++;
      next_data ++; // where crc goes (after data)
    };
    if (context.verbosity > 9)
      fprintf (stderr,
"osdp_build_message: data_length %d new_length now %d next_data now %lx\n",
        data_length, new_length, (unsigned long)next_data);
  };

  // crc
  if (m_check EQUALS OSDP_CRC)
{
  unsigned short int parsed_crc;
  unsigned char *crc_check;
  crc_check = next_data;
  parsed_crc = fCrcBlk (buf, new_length);

  // low order byte first
  *(crc_check+1) = (0xff00 & parsed_crc) >> 8;
  *(crc_check) = (0x00ff & parsed_crc);
  new_length ++;
  new_length ++;
}
  else
  {
    unsigned char
      cksum;
    unsigned char *
      pchecksum;

    pchecksum = next_data;
    cksum = checksum (buf, new_length);
    *pchecksum = cksum;
    new_length ++;
  };

  if (context.verbosity > 9)
  {
    fprintf (stderr, "build: sequence %d. Lth %d\n", sequence, new_length);
  }
  
  *updated_length = new_length;
  return (status);

} /* osdp_build_message */


/*
  parse_message - parses OSDP message

  Note: if verbosity is set (global m_verbosity) it also prints the PDU
  to stderr.
*/
int
  parse_message
    (OSDP_CONTEXT
      *context,
    OSDP_MSG
      *m,
    OSDP_HDR
      *returned_hdr)

{ /* parse_message */

  char
    logmsg [1024];
  unsigned int
    msg_lth;
  int
    msg_check_type;
  int
    msg_data_length;
  int
    msg_scb;
  int
    msg_sqn;
  OSDP_HDR
    *p;
  unsigned short int
    parsed_crc;
  int
    sec_blk_length;
  int
    status;
  unsigned short int
    wire_crc;


  status = ST_MSG_TOO_SHORT;
  logmsg [0] = 0;

  m->data_payload = NULL;
  msg_data_length = 0;
  p = (OSDP_HDR *)m->ptr;

  msg_check_type = (p->ctrl) & 0x04;
  if (msg_check_type EQUALS 0)
    m->check_size = 1;
  else
  {
    m->check_size = 2;
    m_check = OSDP_CRC;
  };

  if (m->lth >= (m->check_size+sizeof (OSDP_HDR)))
  {
    status = ST_OK;
    msg_lth = p->len_lsb + (256*p->len_msb);

    // now that we have a bit of header figure out how much the whole thing is.  need all of it to process it.
    if (m->lth < msg_lth)
      status = ST_MSG_TOO_SHORT;
    else
      osdp_conformance.multibyte_data_encoding.test_status =
        OCONFORM_EXERCISED;
  };
  if (status != ST_OK)
  {
    if (status != ST_MSG_TOO_SHORT)
    {
      fprintf (context->log,
        "parse_message did not clear the header.  msg_data_length %d. msg_check_type 0x%x m->check_size %d. m->lth %d. msg_lth %d status %d.\n",
        msg_data_length, msg_check_type, m->check_size, m->lth, msg_lth,
        status);
      fflush (context->log);
    };
  };
  if (status EQUALS ST_OK)
  {    
    tlogmsg [0] = 0;
    if (context->verbosity > 3)
      // prints low 7 bits of addr field i.e. not request/reply
      sprintf (tlogmsg,
"Addr:%02x Lth:%d. CTRL %02x",
        (0x7f & p->addr), msg_lth, p->ctrl);
   
    // must start with SOM
    if (p->som != C_SOM)
      status = ST_MSG_BAD_SOM;
  };
  if (status EQUALS ST_OK)
  {
    // first few fields are always in same place
    returned_hdr -> som = p->som;
    returned_hdr -> addr = p->addr;
    returned_hdr -> len_lsb = p->len_lsb;
    returned_hdr -> len_msb = p->len_msb;
    returned_hdr -> ctrl = p->ctrl;

    // various control info in CTRL byte
    msg_sqn = (p->ctrl) & 0x03;
    msg_scb = (p->ctrl) & 0x08;

    // depending on whether it's got a security block or not
    // the command/data starts at a different place
    if (msg_scb EQUALS 0)
    {
      m -> cmd_payload = m->ptr + 5;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
    }
    else
    {
fprintf (stderr, "Parsing security block...\n");
//vegas      m -> cmd_payload = m->ptr + sizeof (OSDP_HDR) + 1; // STUB for sec hdr.
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      sec_blk_length = (unsigned)*(m->ptr + 5);
      m -> cmd_payload = m->ptr + 5 + sec_blk_length;

      // whole thing less 5 hdr less 1 cmd less sec blk less 2 crc
      msg_data_length = msg_data_length - 6 - sec_blk_length - 2;

      fflush (stdout);fflush (stderr);
      if (context->verbosity > 3)
      {
        fprintf (stderr, "sec blk lth %d\n", sec_blk_length);
      };
fprintf (stderr, "mlth %d slth %d cmd 0x%x\n",
  msg_data_length, sec_blk_length, *(m->cmd_payload));
    };

    // extract the command
    returned_hdr -> command = (unsigned char) *(m->cmd_payload);
    m->msg_cmd = returned_hdr->command;

    if ((context->verbosity > 2) || (m->msg_cmd != OSDP_ACK))
    {
      sprintf (tlogmsg2, " Cmd %02x", returned_hdr->command);
      strcat (tlogmsg, tlogmsg2);
    };
///    msg_data_length = 0; // depends on command
    if ((context->verbosity > 4) || ((m->msg_cmd != OSDP_POLL) &&
       (m->msg_cmd != OSDP_ACK)))
      {
        char
          dirtag [1024];
        int i;
        unsigned char
          *p1;
        unsigned char
          *p2;
        char
          tlogmsg [1024];


        strcpy (tlogmsg, "");
        p1 = m->ptr;
        if (*(p1+1) & 0x80)
          strcpy (dirtag, "PD");
        else
          strcpy (dirtag, "CP");
        if (context->verbosity > 8)
        {
          int i;
          char line [1024];
          int len;
          char octet [8];
          len = (*(p1+3))*256+*(p1+2);
          strcpy (line, "      Raw: ");
          for (i=0; i<len; i++)
          {
            sprintf (octet, " %02x", *(p1+i));
            strcat (line, octet);
          };
          strcat (line, "\n");
          strcat (tlogmsg, line);
        };
        if (0 EQUALS strcmp (dirtag, "CP"))
          status = oosdp_log (context, OSDP_LOG_STRING_CP, 1, tlogmsg);
        else
          status = oosdp_log (context, OSDP_LOG_STRING_PD, 1, tlogmsg);
      
        p2 = p1+5;
        if (p->ctrl & 0x08)
        {
          fprintf (context->log,
            "  SEC_BLK_LEN %02x SEC_BLK_TYPE %02x SEC_BLK_DATA[0] %02x\n",
            *(p1+5), *(p1+6), *(p1+7));
          p2 = p1+5+*(p1+5); // before-secblk and secblk
        };
        // was fprintf (context->log, "   CMND/REPLY %02x\n", *p2);
        if (!m_dump)
        {
          if (msg_data_length)
          {
            fprintf (context->log,
              "  DATA (%d. bytes):\n    ",
              msg_data_length);
          };
          p1 = m->ptr + (msg_lth-msg_data_length-2);
          for (i=0; i<msg_data_length; i++)
          {
            fprintf (context->log, " %02x", *(i+p1));
          };
          fprintf (context->log, "\n");

          fprintf (context->log, " Raw data: ");
          for (i=0; i<msg_lth; i++)
          {
            fprintf (context->log, " %02x", *(i+m->ptr));
          };
          fprintf (context->log, "\n");
        };
      };

    // go check the command field
    osdp_conformance.CMND_REPLY.test_status = OCONFORM_EX_GOOD_ONLY;
    switch (returned_hdr->command)
    {
    default:
      osdp_conformance.CMND_REPLY.test_status = OCONFORM_UNTESTED;
      //status = ST_PARSE_UNKNOWN_CMD;
      m->data_payload = m->cmd_payload + 1;
      fprintf (stderr, "Unknown command, default msg_data_length was %d\n",
        msg_data_length);
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "\?\?\?");
      break;

    case OSDP_ACK:
      m->data_payload = NULL;
      msg_data_length = 0;
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_ACK");
      context->pd_acks ++;
      osdp_conformance.cmd_poll.test_status = OCONFORM_EXERCISED;
      osdp_conformance.rep_ack.test_status = OCONFORM_EXERCISED;
      if (osdp_conformance.conforming_messages < PARAM_MMT)
        osdp_conformance.conforming_messages ++;
      break;

    case OSDP_BUSY:
      m->data_payload = NULL;
      msg_data_length = 0;
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_BUSY");
      osdp_conformance.resp_busy.test_status = OCONFORM_EXERCISED;
      break;

    case OSDP_CCRYPT:
      m->data_payload = m->cmd_payload + 1;
      if (context->verbosity > 2)
      {
        if (context->role EQUALS OSDP_ROLE_PD)
        {
          strcpy (tlogmsg2, "osdp_CHLNG");
        }
        else
        {
          strcpy (tlogmsg2, "osdp_CCRYPT");
        };
      };
      break;

// OSDP_CHLNG

    case OSDP_MFG:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_MFG");
      break;

    case OSDP_NAK:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      context->sent_naks ++;
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_NAK");
      break;

    case OSDP_BUZ:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_BUZ");
      break;

    case OSDP_CAP:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_CAP");
      break;

    case OSDP_COM:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_COM");
      break;

    case OSDP_COMSET:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = 5;
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_COMSET");
      break;

    case OSDP_ID:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_ID");
      break;

    case OSDP_KEYPAD:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_KEYPAD");
      break;

    case OSDP_LED:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_LED");
      break;

    case OSDP_LSTAT:
      m->data_payload = NULL;
      msg_data_length = 0;
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_LSTAT");
      break;

   case OSDP_LSTATR:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_LSTATR");
      break;

    case OSDP_MFGREP:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_MFGREP");
      break;

    case OSDP_OSTATR:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_OSTATR");
      break;

    case OSDP_OUT:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_OUT");
      break;

    case OSDP_PDCAP:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_PDCAP");
      osdp_conformance.cmd_pdcap.test_status = OCONFORM_EXERCISED;
      osdp_conformance.rep_device_capas.test_status = OCONFORM_EXERCISED;
      break;

    case OSDP_PDID:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
// ASSUMES NO SECURITY
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_PDID");
      osdp_conformance.cmd_id.test_status = OCONFORM_EXERCISED;
      osdp_conformance.rep_device_ident.test_status = OCONFORM_EXERCISED;
      break;

    case OSDP_POLL:
      m->data_payload = NULL;
      msg_data_length = 0;
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_POLL");
      context->cp_polls ++;
      if (osdp_conformance.conforming_messages < PARAM_MMT)
        osdp_conformance.conforming_messages ++;
      break;

    case OSDP_RAW:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_RAW");
      break;

    case OSDP_RSTAT:
      m->data_payload = NULL;
      msg_data_length = 0;
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_RSTAT");
      break;

    case OSDP_RSTATR:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_RSTATR");
      break;

    case OSDP_TEXT:
      m->data_payload = m->cmd_payload + 1;
      msg_data_length = p->len_lsb + (p->len_msb << 8);
      msg_data_length = msg_data_length - 6 - 2; // less hdr,cmnd, crc/chk
      if (context->verbosity > 2)
        strcpy (tlogmsg2, "osdp_TEXT");
      break;
    };

    // for convienience save the data payload length

    m->data_length = msg_data_length;

    if (m->check_size EQUALS 2)
    {
      // figure out where crc or checksum starts
      m->crc_check = m->cmd_payload + 1 + msg_data_length;
      if (0)///(msg_scb)
      {
         fprintf (stderr, "enc mac so +4 check\n");
          m->crc_check = m->crc_check + sec_blk_length;
      };
      parsed_crc = fCrcBlk (m->ptr, m->lth - 2);

      wire_crc = *(1+m->crc_check) << 8 | *(m->crc_check);
      if (parsed_crc != wire_crc)
        status = ST_BAD_CRC;

    }
    else
    {
      unsigned parsed_cksum;
      unsigned wire_cksum;

      // checksum

      parsed_cksum = checksum (m->ptr, m->lth-1);
// checksum is in low-order byte of 16 bit message suffix
      wire_cksum = *(m->cmd_payload + 2 + msg_data_length);
//experimental
if (m->lth == 7)
  wire_cksum = *(m->cmd_payload + 1 + msg_data_length);
      if (context->verbosity > 99)
      {
        fprintf (stderr, "pck %04x wck %04x\n",
          parsed_cksum, wire_cksum);
      };
      if (parsed_cksum != wire_cksum)
      {
        status = ST_BAD_CHECKSUM;
        context->checksum_errs ++;
      };
    };
    if ((context->verbosity > 2) || (m_dump > 0))
    {
      char
        log_line [1024];

      sprintf (log_line, "  Message: %s %s", tlogmsg2, tlogmsg);
      sprintf (tlogmsg2, " Seq:%02x ChkType %x Sec %x CRC: %04x",
        msg_sqn, msg_check_type, msg_scb, wire_crc);
      strcat (log_line, tlogmsg2);
      if (((returned_hdr->command != OSDP_POLL) &&
        (returned_hdr->command != OSDP_ACK)) ||
        (context->verbosity > 3))
        fprintf (context->log, "%s\n", log_line);
      tlogmsg [0] = 0; tlogmsg2 [0] = 0;
    };
  };
  if (status EQUALS ST_OK)
  {
    /*
      at this point we think it's a whole well-formed frame.  might not be for
      us but it's a frame.
    */
    context->packets_received ++;

    if (context->role EQUALS OSDP_ROLE_PD)
      if ((p_card.addr != p->addr) && (p->addr != 0x7f))
      {
        if (context->verbosity > 3)
          fprintf (stderr, "addr mismatch for: %02x me: %02x\n",
            p->addr, p_card.addr);
        status = ST_NOT_MY_ADDR;
      };
    if (context->role EQUALS OSDP_ROLE_MONITOR)
    {
      // pretty print the message if tehre are juicy details.
      (void)monitor_osdp_message (context, m);

      status = ST_MONITOR_ONLY;
    };
  };

  // if there was an error dump the log buffer

  if ((status != ST_OK) && (status != ST_MSG_TOO_SHORT))
  {
    if (strlen (logmsg) > 0)
      fprintf (context->log, "%s\n", logmsg);

    // if parse failed report the status code
    if ((context->verbosity > 3) && (status != ST_MONITOR_ONLY))
    {
      fflush (context->log);
      fprintf (context->log,
        "Message input parsing failed, status %d\n", status);
    };
  };
  return (status);

} /* parse_message */


int
  monitor_osdp_message
    (OSDP_CONTEXT
      *context,
     OSDP_MSG
       *msg)

{ /* monitor_osdp_message */

  time_t
    current_time;
  int
    status;
  char
    tlogmsg [1024];


  status = ST_OK;
  switch (msg->msg_cmd)
  {
  case OSDP_KEYPAD:
    status = oosdp_make_message (OOSDP_MSG_KEYPAD, tlogmsg, msg);
    if (status == ST_OK)
      status = oosdp_log (context, OSDP_LOG_NOTIMESTAMP, 1, tlogmsg);
    break;

  case OSDP_PDCAP:
    status = oosdp_make_message (OOSDP_MSG_PD_CAPAS, tlogmsg, msg);
    if (status == ST_OK)
      status = oosdp_log (context, OSDP_LOG_NOTIMESTAMP, 1, tlogmsg);
    break;

  case OSDP_PDID:
    status = oosdp_make_message (OOSDP_MSG_PD_IDENT, tlogmsg, msg);
    if (status == ST_OK)
      status = oosdp_log (context, OSDP_LOG_NOTIMESTAMP, 1, tlogmsg);
    break;

  case OSDP_RAW:
    // in monitor mode we're really not supposed to use 'action' routines
    // but all it does is printf.

    status = action_osdp_RAW (context, msg);
    break;
  };
  (void) time (&current_time);
  if ((current_time - previous_time) > 15)
  {
    status = oosdp_make_message (OOSDP_MSG_PKT_STATS, tlogmsg, msg);
    if (status == ST_OK)
      status = oosdp_log (context, OSDP_LOG_STRING, 1, tlogmsg);
    previous_time = current_time;
  };
  return (status);

} /* monitor_osdp_message */

